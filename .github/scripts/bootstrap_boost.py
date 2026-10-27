"""Fetch pinned Boost headers where a suitable system package is unavailable."""
import hashlib
from pathlib import Path
import subprocess
import sys
import tarfile
import urllib.request


def main():
    work = Path("build-deps")
    work.mkdir(exist_ok=True)
    archive = work / "boost_1_87_0.tar.gz"
    if not archive.exists():
        urllib.request.urlretrieve(
            "https://archives.boost.io/release/1.87.0/source/boost_1_87_0.tar.gz", archive)
    if hashlib.sha256(archive.read_bytes()).hexdigest() != "f55c340aa49763b1925ccf02b2e83f35fdcf634c9d5164a2acb87540173c741d":
        raise RuntimeError("Boost archive checksum mismatch")
    with tarfile.open(archive) as packed:
        members = [p for p in packed.getmembers() if p.name.startswith("boost_1_87_0/boost/")]
        packed.extractall(work, members=members, filter="data")
    subprocess.run([sys.executable, str(Path(__file__).with_name("stage_boost.py")),
                    str(work / "boost_1_87_0/boost"), str(work / "boost")], check=True)


if __name__ == "__main__":
    main()
