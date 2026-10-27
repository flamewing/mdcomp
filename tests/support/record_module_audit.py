"""Record a reviewed successful run of the moduled assembly tests."""
import argparse
import hashlib
import json
from pathlib import Path
import re


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--test-dir", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    root = args.test_dir
    images = json.loads((root / "module_images.json").read_text())["images"]
    records = {image["name"]: dict(image, cases={}, interruptions=0, transfers=0, negative_cases=0) for image in images}
    total = 0
    for filename, mode in (("module-bootstrap.tsv", "bootstrap"), ("module-encode.tsv", "encoder")):
        for row in (root / filename).read_text().splitlines():
            fields = row.split("\t")
            if len(fields) != 17 or fields[-1] != "1":
                raise ValueError("Invalid/failing module report row: " + row)
            name = fields[0]
            record = records[name]
            record["cases"][mode] = record["cases"].get(mode, 0) + 1
            record["interruptions"] += int(fields[12])
            record["transfers"] += int(fields[11])
            record["negative_cases"] += fields[8] == "0"
            total += 1
    for name, record in records.items():
        if not record["cases"].get("bootstrap") or not record["cases"].get("encoder") or not record["interruptions"]:
            raise ValueError("Unqualified module image: " + name)
        image = (root / "images" / (name + ".bin")).read_bytes()
        if image[:4] != b"MDM6":
            raise ValueError("Unexpected module header: " + name)
        record["image_sha256"] = hashlib.sha256(image).hexdigest()
        for i, key in enumerate(("entry", "payload", "completion", "interrupt_entry", "decoder_begin", "decoder_end", "dma_word_abi")):
            record[key] = int.from_bytes(image[4 + 4 * i:8 + 4 * i], "big")
        if record["dma_word_abi"] != record["dma_source_words"]:
            raise ValueError("DMA ABI export mismatch: " + name)
        hashes = {}

        def source_hash(relative):
            if relative in hashes:
                return
            source = root / "images" / relative
            hashes[relative] = hashlib.sha256(source.read_bytes()).hexdigest()
            for include in re.findall(r'^\s*include\s+"([^"]+)"', source.read_text(), re.MULTILINE | re.IGNORECASE):
                source_hash(include)

        source_hash(name + ".driver.asm")
        record["source_sha256"] = hashes
    summary = {"schema_version": 1, "recorded_cases": total,
               "scope": "Shipped moduled queues with deterministic deferred DMA and real level-6 CPU exceptions; two source address ABIs",
               "qualification": "Logical queue/interrupt integration, not cycle-exact VDP/DMA or real-hardware validation",
               "images": list(records.values())}
    args.output.write_bytes((json.dumps(summary, indent=2) + "\n").encode())
    print("Recorded", total, "passing cases across", len(records), "moduled images")


if __name__ == "__main__":
    main()
