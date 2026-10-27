"""Exercise publishing failure/retry boundaries without contacting GitHub."""
import json
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch
import zipfile

SCRIPTS = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SCRIPTS))
from release import Release, required_targets
import package
import prepare_release


class API:
    def __init__(self):
        self.releases = []
        self.ref = None
        self.writes = []
        self.last_sha = None

    def call(self, method, path, data=None, missing=False):
        if path.startswith("/actions/workflows/"):
            return {"workflow_runs": [{"head_sha": self.last_sha}] if self.last_sha else []}
        if path.startswith("/actions/runs/"):
            return {"created_at": "2020-01-01T23:59:59Z"}
        if path.startswith("/git/tags/"):
            return {"object": {"type": "commit", "sha": "abc"}}
        if method == "GET" and path.startswith("/git/ref/"):
            return self.ref
        if method == "GET" and path.startswith("/releases?"):
            return self.releases
        if method == "POST" and path == "/releases":
            item = dict(data, id=123)
            self.releases.append(item)
            self.writes.append((method, path))
            return item
        if path.startswith("/releases/"):
            item = next((p for p in self.releases if str(p["id"]) == path.split("/")[-1]), None)
            if method == "GET":
                return item
            if method == "DELETE":
                self.releases.remove(item)
            elif method == "PATCH":
                item.update(data)
            else:
                raise AssertionError(method)
            self.writes.append((method, path))
            return item
        raise AssertionError((method, path))


class ReleaseTests(unittest.TestCase):
    def setUp(self):
        self.release = Release("owner/repo", "456", "abc", "snapshot-v0.2.0-run456", "v0.2.0-run456")
        self.api = API()

    def draft(self):
        return self.release.prepare(self.api)

    def files(self, directory):
        for name in self.release.required:
            target = name.removeprefix("mdcomp-").removesuffix("-" + self.release.build_id + ".zip")
            with zipfile.ZipFile(Path(directory) / name, "w") as archive:
                archive.writestr("BUILD-INFO.json", json.dumps({
                    "target": target, "build_id": self.release.build_id, "commit": self.release.sha}))
                for license in ("README.md", "COPYING.md", "COPYING-asm.md"):
                    archive.writestr(license, "test notice")
                suffix = ".exe" if target.startswith("windows-") else ""
                for tool in package.TOOLS:
                    archive.writestr("bin/" + tool + suffix, b"test tool")

    def test_prepare_retry_reuses_owned_draft(self):
        self.assertTrue(self.draft()["draft"])
        self.assertEqual(self.release.prepare(self.api, "123")["id"], 123)
        self.assertEqual(self.api.writes, [("POST", "/releases")])

    def test_published_release_is_read_only(self):
        self.draft()["draft"] = False
        self.api.writes.clear()
        self.release.publish(self.api, 123)
        self.release.cleanup(self.api, 123)
        self.assertFalse(self.release.prepare(self.api)["draft"])
        self.assertFalse(self.api.writes)

    def test_cleanup_deletes_only_owned_draft_and_never_tags(self):
        self.draft()
        self.api.writes.clear()
        self.release.cleanup(self.api, 123)
        self.release.cleanup(self.api, 123)
        self.assertEqual(self.api.writes, [("DELETE", "/releases/123")])

    def test_cleanup_refuses_mismatched_identity(self):
        for field, value in (("body", "another run"), ("tag_name", "other"), ("target_commitish", "def")):
            api = API()
            self.release.prepare(api)[field] = value
            api.writes.clear()
            with self.subTest(field=field), self.assertRaises(RuntimeError):
                self.release.cleanup(api, 123)
            self.assertFalse(api.writes)

    def test_moved_tag_prevents_cleanup_and_creation(self):
        self.draft()
        self.api.ref = {"object": {"type": "commit", "sha": "wrong"}}
        self.api.writes.clear()
        with self.assertRaises(RuntimeError):
            self.release.cleanup(self.api, 123)
        with self.assertRaises(RuntimeError):
            self.release.prepare(self.api)
        self.assertFalse(self.api.writes)

    def test_stable_release_requires_matching_tag(self):
        release = Release("owner/repo", "456", "abc", "v0.2.0", "v0.2.0", snapshot=False)
        with self.assertRaises(RuntimeError):
            release.prepare(self.api)
        self.api.ref = {"object": {"type": "commit", "sha": "abc"}}
        self.assertFalse(release.prepare(self.api)["prerelease"])

    def test_annotated_tag_is_resolved_to_commit(self):
        self.api.ref = {"object": {"type": "tag", "sha": "annotated"}}
        self.assertTrue(self.draft()["draft"])

    def test_replaced_draft_cannot_be_claimed(self):
        self.draft()["id"] = 124
        with self.assertRaises(RuntimeError):
            self.release.prepare(self.api, 123)

    def test_deleted_draft_can_be_recreated_on_retry(self):
        self.draft()
        self.release.cleanup(self.api, 123)
        self.assertTrue(self.release.prepare(self.api, 123)["draft"])

    def test_invalid_ids_never_mutate(self):
        for value in (None, "", "0", "-1", "../123"):
            with self.subTest(value=value), self.assertRaises(ValueError):
                self.release.cleanup(self.api, value)
        self.assertFalse(self.api.writes)

    def test_publish_requires_all_native_packages_and_checksums(self):
        draft = self.draft()
        names = self.release.required | {"SHA256SUMS"}
        for missing in names:
            draft["assets"] = [{"name": name, "state": "uploaded", "size": 1} for name in names - {missing}]
            with self.subTest(missing=missing), self.assertRaises(RuntimeError):
                self.release.publish(self.api, 123)
        draft["assets"] = [{"name": name, "state": "uploaded", "size": 1} for name in names]
        self.release.publish(self.api, 123)
        self.assertFalse(draft["draft"])

    def test_upload_validates_full_set_and_provenance_before_cli(self):
        self.draft()
        with tempfile.TemporaryDirectory() as temp, patch("release.subprocess.run") as upload:
            with self.assertRaises(RuntimeError):
                self.release.upload(self.api, 123, temp)
            upload.assert_not_called()
            self.files(temp)
            self.release.upload(self.api, 123, temp)
            self.assertEqual(len((Path(temp) / "SHA256SUMS").read_text().splitlines()), 8)
            upload.assert_called_once()
            upload.reset_mock()
            path = next(Path(temp).glob("*.zip"))
            path.write_bytes(b"broken zip")
            with self.assertRaises(zipfile.BadZipFile):
                self.release.upload(self.api, 123, temp)
            upload.assert_not_called()

    def test_foreign_package_sha_rejected(self):
        self.draft()
        with tempfile.TemporaryDirectory() as temp, patch("release.subprocess.run") as upload:
            self.files(temp)
            path = next(Path(temp).glob("*.zip"))
            with zipfile.ZipFile(path, "w") as archive:
                archive.writestr("BUILD-INFO.json", json.dumps({"commit": "other", "build_id": self.release.build_id}))
            with self.assertRaises(RuntimeError):
                self.release.upload(self.api, 123, temp)
            upload.assert_not_called()

    def test_unknown_archive_is_rejected_before_upload(self):
        self.draft()
        with tempfile.TemporaryDirectory() as temp, patch("release.subprocess.run") as upload:
            self.files(temp)
            (Path(temp) / "unrelated.zip").write_bytes(b"not this run")
            with self.assertRaises(RuntimeError):
                self.release.upload(self.api, 123, temp)
            upload.assert_not_called()

    def test_prepare_metadata_uses_run_creation_date_for_retries(self):
        with tempfile.TemporaryDirectory() as temp:
            output = Path(temp) / "output"
            environment = {"GITHUB_REPOSITORY": "owner/repo", "GITHUB_RUN_ID": "456",
                           "GITHUB_EVENT_NAME": "workflow_dispatch", "GITHUB_OUTPUT": str(output)}
            with patch.dict(os.environ, environment), patch.object(sys, "argv", ["prepare_release.py", "snapshot"]), \
                    patch("prepare_release.GitHub", return_value=self.api), \
                    patch("prepare_release.subprocess.check_output", return_value="abc\n"):
                prepare_release.main()
            self.assertIn("20200101-run456", output.read_text())

    def test_scheduled_success_skips_without_creating_draft(self):
        self.api.last_sha = "abc"
        environment = {"GITHUB_REPOSITORY": "owner/repo", "GITHUB_RUN_ID": "456",
                       "GITHUB_EVENT_NAME": "schedule", "GITHUB_REF_NAME": "master"}
        with patch.dict(os.environ, environment), patch.object(sys, "argv", ["prepare_release.py", "snapshot"]), \
                patch("prepare_release.GitHub", return_value=self.api), \
                patch("prepare_release.subprocess.check_output", return_value="abc\n"):
            prepare_release.main()
        self.assertFalse(self.api.writes)


class PackagingTests(unittest.TestCase):
    def test_catalog_has_eight_required_native_and_one_optional_android(self):
        self.assertEqual(len(required_targets()), 8)
        self.assertNotIn("android-aarch64", required_targets())

    def test_zip_contains_tools_licenses_and_exact_commit(self):
        with tempfile.TemporaryDirectory() as temp:
            stage = Path(temp) / "stage"
            (stage / "bin").mkdir(parents=True)
            for tool in package.TOOLS:
                (stage / "bin" / tool).write_bytes(b"test executable")
            with patch.dict(os.environ, {"RELEASE_SHA": "abc"}):
                archive = package.package(stage, Path(temp) / "out", "v0.2.0", "linux-x86_64", bundle=False)
            with zipfile.ZipFile(archive) as packed:
                self.assertIsNone(packed.testzip())
                self.assertIn("COPYING.md", packed.namelist())
                self.assertIn("COPYING-asm.md", packed.namelist())
                self.assertEqual(json.loads(packed.read("BUILD-INFO.json"))["commit"], "abc")

    def test_missing_executable_or_invalid_target_refuses_package(self):
        with tempfile.TemporaryDirectory() as temp:
            stage = Path(temp)
            for target, build_id in (("other", "v0.2.0"), ("linux-x86_64", "../other")):
                with self.assertRaises(ValueError):
                    package.package(stage, stage, build_id, target, bundle=False)
            with self.assertRaises(RuntimeError):
                package.package(stage, stage, "v0.2.0", "linux-x86_64", bundle=False)

    def test_pe_import_parser(self):
        self.assertEqual(package.runtime_names(" DLL Name: KERNEL32.dll\n DLL Name: libstdc++-6.dll\n"),
                         ["KERNEL32.dll", "libstdc++-6.dll"])



if __name__ == "__main__":
    unittest.main()
