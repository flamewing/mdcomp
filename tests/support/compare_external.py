"""Cross-check external codecs and mdcomp against deterministic raw bytes."""
import argparse
import json
import os
from pathlib import Path

from compare_binaries import corpus, digest, executable, run


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--reference-dir", type=Path, required=True)
    parser.add_argument("--reference-kind", choices=("kens", "kens15", "kenssharp", "snkrle", "accurate-kosinski", "original-saxman", "clownlzss", "clownnemesis", "clownnemesis-accurate"), required=True)
    parser.add_argument("--candidate-dir", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--candidate-runtime-dir", type=Path, action="append", default=[])
    parser.add_argument("--reference-runtime-dir", type=Path, action="append", default=[])
    parser.add_argument("--timeout", type=float, default=5)
    parser.add_argument("--boundaries", action="store_true", help="Add Kosinski/Saxman descriptor, history and window boundary cases")
    args = parser.parse_args()
    if os.name != "nt" and args.reference_kind == "kens":
        parser.error("Original KENS DLLs require Windows and an x86 .NET Framework wrapper")
    if os.name == "nt":
        import ctypes
        ctypes.windll.kernel32.SetErrorMode(0x8003)
    reference = executable(args.reference_dir.resolve(),
                           {"kens": "kens", "kens15": "koscmp", "kenssharp": "KensSharp", "snkrle": "rle", "accurate-kosinski": "kosinski-compress", "original-saxman": "original-saxman", "clownlzss": "clownlzss", "clownnemesis": "clownnemesis-tool", "clownnemesis-accurate": "clownnemesis-tool"}[args.reference_kind])
    if reference is None:
        parser.error("Missing reference executable")
    reference_dir = args.reference_dir.resolve()
    args.output_dir.mkdir(parents=True, exist_ok=True)
    report = {"reference": args.reference_kind, "hashes": {}, "cases": []}
    for path in [reference] + list(reference_dir.glob("*.dll")):
        report["hashes"][path.name] = digest(path)
    for directory in args.reference_runtime_dir:
        for path in directory.glob("*.dll"):
            report["hashes"]["reference-runtime/" + directory.name + "/" + path.name] = digest(path)
    codecs = [("Kosinski", "koscmp"), ("Enigma", "enicmp"),
              ("Nemesis", "nemcmp"), ("Saxman", "saxcmp")]
    if args.reference_kind == "kenssharp":
        codecs += [("Comper", "compcmp"), ("KosinskiPlus", "kosplus")]
    elif args.reference_kind == "snkrle":
        codecs = [("SnKRLE", "snkcmp")]
    elif args.reference_kind == "accurate-kosinski":
        codecs = [("Kosinski", "koscmp")]
    elif args.reference_kind == "original-saxman":
        codecs = [("Saxman", "saxcmp")]
    elif args.reference_kind.startswith("clownnemesis"):
        codecs = [("Nemesis", "nemcmp")]
    elif args.reference_kind == "clownlzss":
        codecs = [("Kosinski", "koscmp"), ("Saxman", "saxcmp"), ("Comper", "compcmp"), ("KosinskiPlus", "kosplus"), ("Rocket", "rockcmp")]

    def reference_command(codec, encode, source, output):
        if args.reference_kind == "accurate-kosinski":
            tool = executable(reference_dir, "kosinski-compress" if encode else "kosinski-decompress")
            if tool is None:
                parser.error("Missing accurate Kosinski tool")
            report["hashes"]["reference-" + tool.name] = digest(tool)
            return [tool, source, output]
        if args.reference_kind == "original-saxman":
            return [reference, "encode" if encode else "decode", source, output]
        if args.reference_kind.startswith("clownnemesis"):
            return [reference, ("-ca" if args.reference_kind.endswith("-accurate") else "-c") if encode else "-d", source, output]
        if args.reference_kind == "clownlzss":
            option = {"Kosinski": "-k", "Saxman": "-s", "Comper": "-c", "KosinskiPlus": "-kp", "Rocket": "-r"}[codec]
            return [reference] + ([] if encode else ["-d"]) + [option, source, output]
        if args.reference_kind == "kens":
            return [reference, reference_dir, codec, "encode" if encode else "decode", source, output]
        if args.reference_kind == "kenssharp":
            return [reference, "-c" if encode else "-d", codec, source, output]
        if args.reference_kind == "kens15":
            tool = executable(reference_dir, dict(codecs)[codec])
            if tool is None:
                parser.error("Missing reference tool for " + codec)
            report["hashes"]["reference-" + tool.name] = digest(tool)
            return [tool] + ([] if encode else ["-x", "0"]) + [source, output]
        return [reference, source, output] + ([] if encode else ["d"])

    for codec, name in codecs:
        candidate = executable(args.candidate_dir.resolve(), name)
        if candidate is None:
            parser.error("Missing candidate tool: " + name)
        report["hashes"][candidate.name] = digest(candidate)
        inputs = list(corpus())
        if args.boundaries and codec in ("Kosinski", "Saxman"):
            inputs += list(corpus((1, 7, 8, 9, 15, 16, 17, 255, 257)))
            inputs += [("seed-crossing", b"A\0A\0" + bytes(28)),
                       ("seed-crossing-distinct", b"A\0A\0B\0" + bytes(26))]
            random_window = dict(corpus((8192,)))["random-seed-12345678-8192"]
            for window in (4096, 8192):
                inputs.append(("window-{}-copy".format(window), random_window[:window] + random_window[:256]))
        if args.reference_kind == "snkrle":
            inputs += [("run-257-then-literals", b"A" * 257 + bytes(range(31))),
                       ("run-258-then-literals", b"A" * 258 + bytes(range(30))),
                       ("final-pair", bytes(range(30)) + b"ZZ"),
                       ("final-run-257", bytes(range(31)) + b"Z" * 257)]
        for case, data in inputs:
            directory = (args.output_dir / codec.lower() / case).resolve()
            directory.mkdir(parents=True, exist_ok=True)
            raw = directory / "input.bin"
            raw.write_bytes(data)
            row = {"codec": codec, "case": case, "input_sha256": digest(raw)}
            for producer in ("reference", "candidate"):
                encoded = directory / (producer + ".compressed")
                encoded.unlink(missing_ok=True)
                command = (reference_command(codec, True, raw, encoded)
                           if producer == "reference" else [candidate, raw, encoded])
                row[producer + "_encode"] = run(command, encoded, args.timeout,
                    args.reference_runtime_dir if producer == "reference" else args.candidate_runtime_dir)
                if row[producer + "_encode"].get("exit_code") != 0 or "sha256" not in row[producer + "_encode"]:
                    continue
                for consumer in ("reference", "candidate"):
                    decoded = directory / (producer + "-decoded-by-" + consumer + ".bin")
                    decoded.unlink(missing_ok=True)
                    decode_input = encoded
                    if args.reference_kind == "snkrle" and producer != consumer:
                        payload = encoded.read_bytes()
                        if len(payload) < 2:
                            continue
                        size = int.from_bytes(payload[:2], "big")
                        if producer == "candidate" and size % 32:
                            row["header_adaptation_error"] = "Candidate byte count is not tile-aligned"
                            continue
                        size = size * 32 if producer == "reference" else size // 32
                        if size > 65535:
                            row["header_adaptation_error"] = "Converted count exceeds mdcomp's header"
                            continue
                        decode_input = encoded.with_suffix(".header-converted")
                        decode_input.write_bytes(size.to_bytes(2, "big") + payload[2:])
                    command = (reference_command(codec, False, decode_input, decoded)
                               if consumer == "reference" else [candidate, "-x", decode_input, decoded])
                    result = run(command, decoded, args.timeout,
                        args.reference_runtime_dir if consumer == "reference" else args.candidate_runtime_dir)
                    result["matches_input"] = (result.get("exit_code") == 0
                                               and result.get("sha256") == row["input_sha256"])
                    row[producer + "_to_" + consumer] = result
            row["passed"] = all(row.get(p + "_to_" + c, {}).get("matches_input", False)
                                for p in ("reference", "candidate") for c in ("reference", "candidate"))
            report["cases"].append(row)
    rows = report["cases"]
    report["summary"] = {"cases": len(rows), "passed": sum(row["passed"] for row in rows),
                         "directions": {p + "_to_" + c: sum(row.get(p + "_to_" + c, {}).get("matches_input", False) for row in rows)
                                        for p in ("reference", "candidate") for c in ("reference", "candidate")}}
    (args.output_dir / "report.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report["summary"]))
    return int(report["summary"]["passed"] != report["summary"]["cases"])


if __name__ == "__main__":
    raise SystemExit(main())
