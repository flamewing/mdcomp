"""Round-trip every packaged CLI, optionally through Node/WebAssembly."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

from package import TOOLS


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("--node", action="store_true")
    parser.add_argument("--node-argument", action="append", default=[])
    args = parser.parse_args()
    with tempfile.TemporaryDirectory() as temp:
        raw = Path(temp) / "input.bin"
        raw.write_bytes(bytes(range(64)))
        for name in TOOLS:
            suffix = ".js" if args.node else ".exe" if os.name == "nt" else ""
            tool = [str((args.directory / (name + suffix)).resolve())]
            if args.node:
                tool = ["node", *args.node_argument, *tool]
            packed, decoded = Path(temp) / (name + ".packed"), Path(temp) / (name + ".raw")
            subprocess.run([*tool, str(raw), str(packed)], check=True, timeout=30)
            subprocess.run([*tool, "-x", str(packed), str(decoded)], check=True, timeout=30)
            if decoded.read_bytes() != raw.read_bytes():
                raise RuntimeError("Packaged CLI round trip failed: " + name)
    print("All ten command-line codecs passed")


if __name__ == "__main__":
    main()
