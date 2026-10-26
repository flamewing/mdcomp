"""Record reviewed hand-authored fixture bytes; never invokes a codec encoder."""
import argparse
import hashlib
import json
from pathlib import Path

SOURCES = {
    "comper": "src/asm/Comper_internal.asm",
    "comperx": "src/asm/ComperX_internal.asm",
    "kosinski": "src/asm/Kosinski_internal.asm",
    "kosplus": "src/asm/KosinskiPlus_internal.asm",
    "saxman": "src/asm/Saxman.asm",
    "rocket": "src/asm/Rocket.asm",
    "enigma": "src/asm/Enigma.asm",
    "nemesis": "src/lib/nemesis.cc: decode_header/decode grammar",
    "lzkn1": "src/lib/lzkn1.cc: decode_edge grammar",
    "snkrle": "src/asm/SNKRLE.asm; C++ odd-byte counts from src/lib/snkrle.cc",
}

REQUIRED_FORMS = {
    "comper": ["literal", "dictionary", "terminator"],
    "comperx": ["literal", "dictionary", "rle", "terminator"],
    "kosinski": ["literal", "inline-match", "short-match", "long-match", "discard-control", "terminator"],
    "kosplus": ["literal", "inline-match", "short-match", "long-match", "terminator"],
    "lzkn1": ["packed-literal", "short-match", "long-match", "maximum-length", "terminator"],
    "saxman": ["literal", "dictionary", "seeded-fill", "size-bound"],
    "rocket": ["dictionary", "seeded-fill", "seed-to-output"],
    "enigma": ["incrementing", "common", "inline-repeat", "inline-increment", "inline-decrement", "inline-literals", "all-flags", "terminator"],
    "nemesis": ["table-code", "inline-rle", "normal", "progressive-xor", "run-length-one", "run-length-eight", "empty"],
    "snkrle": ["literal", "run", "count-255-continuation", "empty"],
}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--export-dir", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    fixtures = {}
    for line in (args.export_dir / "index.tsv").read_text().splitlines():
        name, codec, edges = line.split("\t")
        item = {"codec": codec, "source": SOURCES[codec], "edges": edges.split(","),
                "classification": "hand-constructed", "license": "LGPL-3.0-or-later",
                "expected_source": "synthetic literals, repetitions, or explicit arithmetic sequences"}
        for extension in ("compressed", "raw"):
            data = (args.export_dir / (name + "." + extension)).read_bytes()
            item[extension + "_sha256"] = hashlib.sha256(data).hexdigest()
            item[extension + "_size"] = len(data)
        fixtures[name] = item
    manifest = {"schema_version": 1, "generator": "tests/fixtures/known_answers.cc",
                "independence": "No encoder is called. Assembly/decoder grammar was read, not executed as an independent oracle.",
                "padding": "Bit sequences include zero read-ahead guards; descriptor sequences insert early-refill placeholders explicitly.",
                "required_decoder_forms": REQUIRED_FORMS,
                "fixtures": fixtures}
    args.output.write_text(json.dumps(manifest, sort_keys=True, indent=2) + "\n")
    print("Recorded {} fixtures".format(len(fixtures)))


if __name__ == "__main__":
    main()
