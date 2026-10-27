"""Derive retry-stable release metadata and skip already processed snapshots."""
import argparse
from datetime import datetime
import json
import os
from pathlib import Path
import re
import subprocess

from release import GitHub, Release


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("kind", choices=("snapshot", "stable"))
    args = parser.parse_args()
    repository = os.environ["GITHUB_REPOSITORY"]
    run = os.environ["GITHUB_RUN_ID"]
    sha = subprocess.check_output(["git", "rev-parse", "HEAD"], text=True).strip()
    api = GitHub(repository)
    if args.kind == "snapshot" and os.environ["GITHUB_EVENT_NAME"] != "workflow_dispatch":
        branch = os.environ["GITHUB_REF_NAME"]
        from urllib.parse import urlencode
        query = urlencode({"branch": branch, "status": "success", "per_page": 1})
        previous = api.call("GET", "/actions/workflows/snapshots.yml/runs?" + query)["workflow_runs"]
        if previous and previous[0]["head_sha"] == sha:
            print("This commit already has a successful snapshot run.")
            return
    version = re.search(r"project\(mdcomp\s+VERSION\s+(\S+)", Path("CMakeLists.txt").read_text())[1]
    if json.loads(Path("vcpkg.json").read_text())["version"] != version:
        raise RuntimeError("CMake and vcpkg versions disagree")
    if args.kind == "snapshot":
        created = api.call("GET", "/actions/runs/" + run)["created_at"]
        date = datetime.fromisoformat(created.replace("Z", "+00:00")).strftime("%Y%m%d")
        build_id = f"v{version}.{date}-run{run}"
        tag = "snapshot-" + build_id
    else:
        tag = os.environ.get("REQUESTED_TAG") or os.environ["GITHUB_REF_NAME"]
        if not re.fullmatch(r"v" + re.escape(version) + r"(?:[-+][A-Za-z0-9.-]+)?", tag):
            raise RuntimeError("Release tag must match the project version")
        build_id = tag
    release = Release(repository, run, sha, tag, build_id, args.kind == "snapshot").prepare(api)
    with open(os.environ["GITHUB_OUTPUT"], "a", encoding="utf-8") as out:
        for key, value in {"sha": sha, "build_id": build_id, "tag": tag,
                           "kind": args.kind, "release_id": release["id"] if release["draft"] else ""}.items():
            out.write(f"{key}={value}\n")


if __name__ == "__main__":
    main()
