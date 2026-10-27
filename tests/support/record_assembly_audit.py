"""Summarize a completed assembly CTest run; no decoder or encoder is invoked."""
import argparse
import hashlib
import json
from pathlib import Path
import re


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--test-dir", type=Path, required=True, help="Build directory's tests/asm directory")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    root = args.test_dir
    decoders = json.loads((root / "decoder_images.json").read_text())["decoders"]
    records = {d["name"]: dict(d, cases={}, max_read_ahead_observed=0) for d in decoders}
    for name, record in records.items():
        path = root / "images" / (name + ".bin")
        data = path.read_bytes()
        record["image_sha256"] = digest(path)
        width, endian = (4, "big") if record["architecture"] == "m68000" else (2, "little")
        for i, label in enumerate(("entry", "payload", "completion")):
            record[label] = int.from_bytes(data[4 + i * width:4 + (i + 1) * width], endian)
        if record["self_modifying"]:
            record["writable_instruction_words"] = [int.from_bytes(data[i:i + 2], "little") for i in (10, 12)]
        hashes = {}

        def source_hash(relative):
            if relative in hashes:
                return
            source = root / "images" / relative
            hashes[relative] = digest(source)
            for include in re.findall(r'^\s*include\s+"([^"]+)"', source.read_text(), re.MULTILINE | re.IGNORECASE):
                source_hash(include)

        source_hash(record["source"])
        record["source_sha256"] = hashes
        source_hash(name + ".driver.asm")
        record["provenance"] = ("original-derived game disassembly" if record["original_derived"] else
                                "edited original-derived routine" if record["codec"] == "enigma" else
                                "hand-crafted/reference-derived mdcomp routine; see source authorship")
        record["deployment"] = "Not independently surveyed; no new field-use claim"
        record["verification"] = ["emulator", "hand-constructed expectations", "C++ encoder interoperability"]
        record["configuration"] = "Default shipped settings; word-aligned source/output; see hashed source and wrapper"
        if record["self_modifying"]:
            record["configuration"] = "OptimiseDriver=0; only the two exported instruction words are writable"
    total, failed = 0, []
    for filename, mode in (("bootstrap.tsv", "known-answer"), ("external.tsv", "external-encoding"),
                            ("encode.tsv", "cpp-encoding"), ("rocket-large.tsv", "cpp-encoding"),
                            ("negative.tsv", "containment")):
        rows = (root / filename).read_text().splitlines()
        for line in rows:
            name, case, status, matches, instructions, cycles, high, size, allowance = line.split("\t")
            record = records[name]
            total += 1
            record["cases"][mode] = record["cases"].get(mode, 0) + 1
            passed = status == "1" and matches == "1"
            if not passed:
                failed.append(name + "/" + case)
            observed = max(0, int(high) - int(size))
            if mode != "containment":
                if observed > int(allowance) or int(allowance) != record["readable_padding"]:
                    raise ValueError("Read-ahead qualification mismatch: " + name + "/" + case)
                record["max_read_ahead_observed"] = max(record["max_read_ahead_observed"], observed)
    if failed:
        raise ValueError("Assembly results contain failures: " + ", ".join(failed))
    for name, record in records.items():
        if not record["cases"].get("known-answer") or not record["cases"].get("cpp-encoding"):
            raise ValueError("Missing bootstrap/encoder coverage: " + name)
        if record["cases"].get("external-encoding"):
            record["verification"].append("qualified external encodings; lineage retained in external manifest")
    summary = {"schema_version": 1, "scope": "Default direct decoder configurations only; no moduled hardware queues or real hardware",
               "qualification": "Emulator verified against hand-constructed expectations, applicable saved external encodings, and C++ encoder output. Not an algorithmic independence claim.",
               "recorded_cases": total, "failed_cases": failed, "decoders": list(records.values())}
    args.output.write_bytes((json.dumps(summary, indent=2) + "\n").encode("utf-8"))
    print("Recorded", total, "passing cases across", len(records), "decoder images")


if __name__ == "__main__":
    main()
