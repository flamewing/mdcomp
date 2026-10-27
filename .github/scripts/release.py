"""Publish only complete packages belonging to this workflow run and commit."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
from urllib.parse import quote
import zipfile

from package import TOOLS

CATALOG = Path(__file__).resolve().parents[1] / "packages.json"


def required_targets():
    data = json.loads(CATALOG.read_text())
    return {item["id"] for item in data["native"]} | set(data["required_vm"])


class GitHub:
    def __init__(self, repository):
        self.repository = repository

    def call(self, method, path, data=None, missing=False):
        args = ["gh", "api", "--method", method, f"repos/{self.repository}{path}"]
        if data is not None:
            args += ["--input", "-"]
        result = subprocess.run(args, input=json.dumps(data) if data is not None else None,
                                text=True, capture_output=True)
        if result.returncode:
            if missing and "(HTTP 404)" in result.stderr:
                return None
            raise RuntimeError(result.stderr)
        return json.loads(result.stdout) if result.stdout.strip() else None


class Release:
    def __init__(self, repository, run, sha, tag, build_id, snapshot=True):
        if not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9_.+-]*", build_id):
            raise ValueError("Invalid build ID")
        self.repository, self.run, self.sha = repository, run, sha
        self.tag, self.build_id, self.snapshot = tag, build_id, snapshot
        self.marker = f"<!-- mdcomp-release-run:{repository}:{run} sha:{sha} -->"

    def ref_matches(self, api):
        ref = api.call("GET", "/git/ref/tags/" + quote(self.tag, safe=""), missing=True)
        if ref is None:
            if not self.snapshot:
                raise RuntimeError("Stable releases require an existing tag")
            return
        target = ref["object"]
        for _ in range(20):
            if target["type"] != "tag":
                break
            target = api.call("GET", "/git/tags/" + target["sha"])["object"]
        if target["type"] != "commit" or target["sha"] != self.sha:
            raise RuntimeError("Release tag points at another commit")

    def owned(self, api, release):
        if (release["tag_name"] != self.tag or release["target_commitish"] != self.sha
                or self.marker not in (release.get("body") or "").splitlines()
                or not isinstance(release["id"], int) or not isinstance(release["draft"], bool)):
            raise RuntimeError("Release is not owned by this run and commit")
        self.ref_matches(api)
        return release

    def path(self, release_id):
        if not str(release_id).isdigit() or int(release_id) <= 0:
            raise ValueError("Invalid recorded release ID")
        return "/releases/" + str(release_id)

    def find(self, api):
        page = 1
        while True:
            items = api.call("GET", f"/releases?per_page=100&page={page}")
            for item in items:
                if item["tag_name"] == self.tag:
                    return item
            if len(items) < 100:
                return None
            page += 1

    def prepare(self, api, release_id=None):
        self.ref_matches(api)
        release = self.find(api) if release_id is None else api.call(
            "GET", self.path(release_id), missing=True)
        if release is None:
            if release_id is not None and self.find(api) is not None:
                raise RuntimeError("The recorded draft was replaced")
            release = api.call("POST", "/releases", {
                "tag_name": self.tag, "target_commitish": self.sha,
                "name": ("Snapshot " if self.snapshot else "mdcomp ") + self.build_id,
                "draft": True, "prerelease": self.snapshot,
                "body": f"Build `{self.build_id}` from `{self.sha}`.\n\n"
                        "Packages include command-line tools, libraries, headers, and licenses. "
                        "Android packages are experimental cross builds, not device-tested.\n\n"
                        + self.marker})
        return self.owned(api, release)

    @property
    def required(self):
        return {f"mdcomp-{target}-{self.build_id}.zip" for target in required_targets()}

    def upload(self, api, release_id, directory):
        release = self.owned(api, api.call("GET", self.path(release_id)))
        if not release["draft"]:
            return
        files = sorted(Path(directory).glob("*.zip"))
        names = {p.name for p in files if p.is_file() and p.stat().st_size}
        optional = json.loads(CATALOG.read_text())["optional"]
        allowed = self.required | {f"mdcomp-{target}-{self.build_id}.zip" for target in optional}
        if not self.required <= names or names - allowed or len(names) != len(files):
            raise RuntimeError("Incomplete or unrelated release packages")
        for path in files:
            with zipfile.ZipFile(path) as archive:
                if archive.testzip() is not None:
                    raise RuntimeError("Corrupted package: " + path.name)
                info = json.loads(archive.read("BUILD-INFO.json"))
                if (info["build_id"] != self.build_id or info["commit"] != self.sha
                        or path.name != f"mdcomp-{info['target']}-{self.build_id}.zip"):
                    raise RuntimeError("Package provenance does not match this release")
                suffix = ".exe" if info["target"].startswith("windows-") else ""
                expected = {"BUILD-INFO.json", "README.md", "COPYING.md", "COPYING-asm.md"}
                expected.update("bin/" + tool + suffix for tool in TOOLS)
                if not expected <= set(archive.namelist()):
                    raise RuntimeError("Package is missing required tools or licenses")
                if any(Path(name).is_absolute() or ".." in Path(name).parts for name in archive.namelist()):
                    raise RuntimeError("Package contains an unsafe extraction path")
        checksums = Path(directory) / "SHA256SUMS"
        checksums.write_text("".join(
            f"{hashlib.sha256(p.read_bytes()).hexdigest()}  {p.name}\n" for p in files), encoding="utf-8")
        subprocess.run(["gh", "release", "upload", self.tag, "--repo", self.repository,
                        *map(str, files), str(checksums), "--clobber"], check=True)

    def publish(self, api, release_id):
        path = self.path(release_id)
        release = self.owned(api, api.call("GET", path))
        if not release["draft"]:
            return
        assets = {p["name"] for p in release.get("assets", [])
                  if p.get("state") == "uploaded" and p.get("size", 0) > 0}
        if not (self.required | {"SHA256SUMS"}) <= assets:
            raise RuntimeError("Required uploaded packages or checksums are missing")
        api.call("PATCH", path, {"draft": False})

    def cleanup(self, api, release_id):
        path = self.path(release_id)
        release = api.call("GET", path, missing=True)
        if release is not None and self.owned(api, release)["draft"]:
            api.call("DELETE", path)
        # Never delete tags or a published release, even after a later failure.


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=("prepare", "upload", "publish", "cleanup"))
    parser.add_argument("--release-id")
    parser.add_argument("--asset-dir", default="release-assets")
    args = parser.parse_args()
    release = Release(os.environ["GITHUB_REPOSITORY"], os.environ["GITHUB_RUN_ID"],
                      os.environ["RELEASE_SHA"], os.environ["RELEASE_TAG"],
                      os.environ["BUILD_ID"], os.environ["RELEASE_KIND"] == "snapshot")
    api = GitHub(release.repository)
    if args.command == "prepare":
        prepared = release.prepare(api, args.release_id)
        with open(os.environ["GITHUB_OUTPUT"], "a", encoding="utf-8") as out:
            out.write(f"release_id={prepared['id'] if prepared['draft'] else ''}\n")
    elif args.command == "upload":
        release.upload(api, args.release_id, args.asset_dir)
    elif args.command == "publish":
        release.publish(api, args.release_id)
    else:
        release.cleanup(api, args.release_id)


if __name__ == "__main__":
    main()
