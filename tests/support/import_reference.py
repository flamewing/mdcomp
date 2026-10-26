"""Explicitly import and verify the pinned published master-branch Windows tools."""
import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import urllib.request
import zipfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--destination", type=Path, required=True)
    parser.add_argument("--archive", type=Path, help="Use an already downloaded archive")
    args = parser.parse_args()
    manifest_path = Path(__file__).resolve().parents[1] / "references" / "master.json"
    manifest = json.loads(manifest_path.read_text())
    pin = manifest["published_snapshot"]
    args.destination.mkdir(parents=True, exist_ok=True)
    archive = args.archive or args.destination.parent / pin["archive_name"]
    if not archive.exists():
        urllib.request.urlretrieve(pin["download_url"], str(archive))
    actual = hashlib.sha256(archive.read_bytes()).hexdigest()
    if actual != pin["archive_sha256"]:
        raise SystemExit("Reference archive SHA-256 mismatch: " + actual)
    root = args.destination.resolve()
    with zipfile.ZipFile(str(archive)) as package:
        for member in package.infolist():
            relative = PurePosixPath(member.filename.replace("\\", "/"))
            if relative.is_absolute() or ".." in relative.parts or ":" in member.filename:
                raise SystemExit("Unsafe reference archive member: " + member.filename)
            target = (root / str(relative)).resolve()
            if root not in target.parents and target != root:
                raise SystemExit("Reference archive member escapes destination")
        package.extractall(str(root))
    for filename, expected in pin["executable_sha256"].items():
        if hashlib.sha256((root / filename).read_bytes()).hexdigest() != expected:
            raise SystemExit("Reference executable SHA-256 mismatch: " + filename)
    print("Verified {} at {}".format(pin["tag"], root))


if __name__ == "__main__":
    main()
