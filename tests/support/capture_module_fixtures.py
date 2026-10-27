"""Capture qualified full-module and tail bodies from reviewed external audits."""
import argparse
import hashlib
import json
from pathlib import Path


def digest(data):
    return hashlib.sha256(data).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--comparison-root", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    args = parser.parse_args()
    repo = Path(__file__).resolve().parents[2]
    pins = json.loads((repo / "tests/references/external.json").read_text())["references"]
    fixtures, generated, reports, references = {}, [], {}, {}
    codecs = dict(Comper="comper", Kosinski="kosinski", KosinskiPlus="kosplus",
                  Rocket="rocket", Saxman="saxman", SnKRLE="snkrle")
    for kind, pin, baseline in (("clownlzss", "clownlzss", "clownlzss-comparison"),
                                ("kens", "kens-rc3", "kens-rc3-comparison"),
                                ("snkrle", "snkrle", "snkrle-comparison")):
        root = args.comparison_root / (kind + "-module-blocks")
        report_bytes = (root / "report.json").read_bytes()
        report = json.loads(report_bytes.decode())
        old = json.loads((args.comparison_root / baseline / "report.json").read_text())
        # A newly rebuilt tool needs separate qualification, not silent reuse
        # of the source pin attached to the earlier audited executable.
        names = {"clownlzss": ("clownlzss", "clownlzss.exe"),
                 "snkrle": ("rle", "rle.exe"), "kens": ("kens.exe",)}[kind]
        def reference_hashes(hashes):
            return {name: value for name, value in hashes.items()
                    if name in names or name.lower().endswith(".dll")}
        hashes = reference_hashes(report["hashes"])
        if not hashes or hashes != reference_hashes(old["hashes"]):
            raise ValueError("Reference binary identity differs from reviewed audit: " + kind)
        references[pin] = dict(source_pin=pins[pin], binary_sha256=hashes)
        rows = {(row["codec"], row["case"]): row for row in report["cases"]}
        reports[kind] = dict(report_sha256=digest(report_bytes), summary=report["summary"],
                             unqualified=[dict(codec=r["codec"], case=r["case"],
                                 encode_status=r["reference_encode"]["status"])
                                 for r in report["cases"] if not r["passed"]])
        for label, codec in codecs.items():
            if ((kind == "kens" and codec != "kosinski")
                    or (kind == "snkrle" and codec != "snkrle")
                    or (kind == "clownlzss" and codec == "snkrle")):
                continue
            for pattern in ("repeat", "ascending"):
                if kind == "clownlzss" and codec in ("kosinski", "kosplus") and pattern == "repeat":
                    continue  # Outside the declared five-second reference budget.
                pair = [rows.get((label, pattern + "-" + str(size))) for size in (64, 4096)]
                if not all(row and row["passed"] for row in pair):
                    raise ValueError("Declared reference module pair failed: " + "/".join((kind, codec, pattern)))
                for size, row in zip((64, 4096), pair):
                    case = row["case"]
                    folder = root / label.lower() / case
                    raw = (folder / "input.bin").read_bytes()
                    expected = b"A" * size if pattern == "repeat" else bytes(i % 256 for i in range(size))
                    if raw != expected or digest(raw) != row["input_sha256"]:
                        raise ValueError("Unexpected synthetic input")
                    original = (folder / "reference.compressed").read_bytes()
                    compressed = original
                    transformation = "none"
                    if kind == "snkrle":
                        declared = int.from_bytes(original[:2], "big") * 32
                        if declared != size:
                            raise ValueError("Unexpected RLE tile count")
                        compressed = declared.to_bytes(2, "big") + original[2:]
                        transformation = "tile-count header multiplied by 32; token body unchanged"
                    identifier = "module-" + kind + "-" + codec + "-" + case
                    fixtures[identifier] = dict(codec=codec, classification="reference-generated",
                        reference=pin,
                        case=case, recipe=pattern, raw_size=size, raw_sha256=digest(raw),
                        compressed_size=len(compressed), compressed_sha256=digest(compressed),
                        original_compressed_sha256=digest(original), transformation=transformation,
                        edges=["external-reference", "module-block"],
                        qualification="All four self/cross-decodes passed. Container framing is test-owned, not supplied by this reference.")
                    generated.append('{"' + identifier + '", "' + codec + '", hex("' + compressed.hex()
                        + '"), input("' + pattern + '", ' + str(size)
                        + '), {"external-reference", "module-block"}, {}, {}},')
    if set(f["codec"] for f in fixtures.values()) != set(codecs.values()):
        raise ValueError("Missing qualified module codec")
    required = {codec: ["external-reference", "module-block"] for codec in codecs.values()}
    manifest = dict(schema_version=1, generator="tests/support/capture_module_fixtures.py",
                    qualification="Synthetic 4096-byte bodies and 64-byte tails, with paired recipes and retained lineage. No source code or game assets embedded.",
                    reports=reports, references=references, required_decoder_forms=required, fixtures=fixtures)
    args.output_dir.mkdir(parents=True, exist_ok=True)
    (args.output_dir / "external_modules.json").write_bytes((json.dumps(manifest, indent=2) + "\n").encode())
    (args.output_dir / "external_modules.inc").write_bytes(("// Generated by capture_module_fixtures.py; review external_modules.json.\n" + "\n".join(generated) + "\n").encode())
    print("Captured", len(fixtures), "qualified full/tail bodies")


if __name__ == "__main__":
    main()
