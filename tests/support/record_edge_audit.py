"""Record declared edge coverage from a reviewed successful CTest run."""
import argparse
import csv
import hashlib
import json
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--test-dir", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    manifest = json.loads((root / "tests/fixtures/edges.json").read_text())
    reports = {}
    for mode in ("encoder", "decoder"):
        with (args.test_dir / (mode + "-edges.tsv")).open(newline="") as stream:
            rows = list(csv.DictReader(stream, delimiter="\t"))
        if not rows or any(set(row) != {"codec", "edge", "first_case"} for row in rows):
            raise ValueError("Invalid edge report: " + mode)
        reports[mode] = {(row["codec"], row["edge"]): row["first_case"] for row in rows}
    counts = dict(encoder=0, decoder=0, boundary=0)
    records = {}
    for codec, modes in manifest["codecs"].items():
        records[codec] = {}
        for mode, edges in modes.items():
            observed = reports["decoder" if mode == "boundary" else mode]
            records[codec][mode] = {}
            for edge in edges:
                if (codec, edge) not in observed:
                    raise ValueError("Missing obligation: " + "/".join((codec, mode, edge)))
                records[codec][mode][edge] = observed[codec, edge]
                counts[mode] += 1
    hashes = {}
    for path in ("tests/fixtures/edges.json", "tests/fixtures/manifest.json",
                 "tests/support/format_observer.cc", "tests/codec/edge_coverage.cc"):
        hashes[path] = hashlib.sha256((root / path).read_bytes()).hexdigest()
    result = dict(schema_version=1, qualification=manifest["qualification"],
                  source_sha256=hashes, covered_obligations=counts, missing=[], codecs=records)
    args.output.write_bytes((json.dumps(result, indent=2) + "\n").encode())
    print("Recorded covered obligations:", counts)


if __name__ == "__main__":
    main()
