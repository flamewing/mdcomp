"""Explicitly import hash-pinned original-derived decoder sources (never ROM data)."""
import argparse
import hashlib
import json
from pathlib import Path
import urllib.request


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--destination", type=Path, required=True)
    parser.add_argument("--offline-dir", type=Path, help="Directory containing cached full source files")
    args = parser.parse_args()
    manifest = json.loads((Path(__file__).resolve().parents[1] / "references/sega-decoders.json").read_text())
    # Verify every source and extraction before changing the destination.
    outputs = {}
    for entry in manifest["sources"]:
        cache_name = "s2.sounddriver.asm" if "extract_start" in entry else entry["name"]
        if args.offline_dir:
            data = (args.offline_dir / cache_name).read_bytes()
        else:
            request = urllib.request.Request(entry["url"], headers={"User-Agent": "mdcomp-reference-import"})
            with urllib.request.urlopen(request, timeout=30) as response:
                data = response.read(1024 * 1024 + 1)
        if len(data) > 1024 * 1024 or hashlib.sha256(data).hexdigest() != entry["sha256"]:
            raise ValueError("Source size/hash mismatch: " + entry["name"])
        outputs[cache_name] = data
        if "extract_start" in entry:
            text = data.decode("utf-8").replace("\r\n", "\n")
            start = text.index(entry["extract_start"])
            end = text.index(entry["extract_end"], start) + len(entry["extract_end"])
            data = (text[start:end] + "\n").encode("utf-8")
            if hashlib.sha256(data).hexdigest() != entry["extracted_sha256"]:
                raise ValueError("Extracted block hash mismatch: " + entry["name"])
        outputs[entry["name"]] = data
    args.destination.mkdir(parents=True, exist_ok=True)
    for name, data in outputs.items():
        (args.destination / name).write_bytes(data)
    (args.destination / "provenance.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print("Imported", len(manifest["sources"]), "pinned decoder sources")


if __name__ == "__main__":
    main()
