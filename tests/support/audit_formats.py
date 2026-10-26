"""Bounded, test-only format interpreters for adjudicating codec disagreements.

These deliberately use no mdcomp/KENS/Clownacy codec routines. Rules and their
qualifications are documented in tests/references/format-verdicts.md.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path

from compare_binaries import executable, run


class Invalid(ValueError):
    pass


class Reader:
    def __init__(self, data):
        self.data, self.pos = data, 0

    def byte(self):
        if self.pos >= len(self.data):
            raise Invalid("truncated input at byte " + str(self.pos))
        value = self.data[self.pos]
        self.pos += 1
        return value

    def word(self):
        return (self.byte() << 8) | self.byte()


class Bits:
    def __init__(self, reader):
        self.reader, self.value, self.left = reader, 0, 0

    def get(self, count):
        value = 0
        for _ in range(count):
            if not self.left:
                self.value, self.left = self.reader.byte(), 8
            self.left -= 1
            value = (value << 1) | ((self.value >> self.left) & 1)
        return value


def decode(codec, data, limit=1024 * 1024):
    source, out, trace = Reader(data), bytearray(), []

    def event(kind, **fields):
        trace.append(dict(kind=kind, input_byte=source.pos, output_bytes=len(out), **fields))
        if len(out) > limit or len(trace) > limit:
            raise Invalid("audit execution/output limit")

    def copy(distance, length):
        if not 1 <= distance <= len(out):
            raise Invalid("dictionary reference before available history")
        for _ in range(length):
            out.append(out[-distance])

    if codec == "saxman":
        size = source.byte() | (source.byte() << 8)
        if size > len(data) - 2:
            raise Invalid("Saxman declared body length exceeds available bytes")
        end, descriptor, left = source.pos + size, 0, 0
        while source.pos < end:
            if not left:
                descriptor, left = source.byte(), 8
                if source.pos == end:
                    break  # Unused descriptor bits at the compressed-size boundary.
            literal = descriptor & 1
            descriptor >>= 1
            left -= 1
            if literal:
                out.append(source.byte())
                event("literal")
            else:
                if source.pos + 2 > end:
                    raise Invalid("Saxman body ends inside a dictionary token")
                low, high = source.byte(), source.byte()
                length = (high & 15) + 3
                origin = ((((high & 240) << 4) | low) + 18 - len(out)) % 4096 + len(out) - 4096
                if origin < 0:
                    out.extend(bytes(length))
                    event("zero-fill", source=origin, length=length)
                else:
                    copy(len(out) - origin, length)
                    event("dictionary", source=origin, length=length)
    elif codec == "kosinski":
        descriptor = source.byte() | (source.byte() << 8)
        left = 16

        def bit():
            nonlocal descriptor, left
            value = descriptor & 1
            descriptor >>= 1
            left -= 1
            if not left:  # Kosinski reloads before the last command's payload.
                descriptor, left = source.byte() | (source.byte() << 8), 16
            return value

        while True:
            if bit():
                out.append(source.byte())
                event("literal")
                continue
            if bit():
                low, high = source.byte(), source.byte()
                distance, length = 8192 - (low | ((high & 248) << 5)), high & 7
                if length:
                    length += 2
                else:
                    length = source.byte() + 1
                    if length == 1:
                        event("terminator")
                        break
                    if length == 2:
                        event("discard-control")
                        continue
            else:
                length = 2 + 2 * bit() + bit()
                distance = 256 - source.byte()
            copy(distance, length)
            event("dictionary", distance=distance, length=length)
    elif codec == "enigma":
        width, flags, increment, common = source.byte(), source.byte(), source.word(), source.word()
        if width > 11 or flags & ~31:
            raise Invalid("Enigma header exceeds tile-index/flag fields")
        bits = Bits(source)

        def inline():
            value = 0
            for flag in range(4, -1, -1):
                if flags & (1 << flag):
                    value |= bits.get(1) << (flag + 11)
            return value | bits.get(width)

        while True:
            kind = bits.get(2)
            if kind & 2:
                kind = (kind << 1) | bits.get(1)
            length = bits.get(4) + 1
            if kind == 7 and length == 16:
                event("terminator")
                break
            value = increment if kind == 0 else common if kind == 1 else 0 if kind == 7 else inline()
            for _ in range(length):
                word = inline() if kind == 7 else value
                out.extend((word & 65535).to_bytes(2, "big"))
                value += 1 if kind in (0, 5) else -1 if kind == 6 else 0
            if kind == 0:
                increment = value & 65535
            event("packet", packet_type=kind, words=length)
    elif codec == "nemesis":
        header = source.word()
        required = (header & 32767) * 64
        if not required:
            # Empty C++ contract: no output or table is needed for zero tiles.
            return b"", [{"kind": "zero-tile C++ contract"}]
        if required // 2 > limit:
            raise Invalid("Nemesis declared output exceeds audit limit")
        table, palette = {}, 0
        while True:
            token = source.byte()
            if token == 255:
                break
            if token & 128:
                palette, token = token & 15, source.byte()
            length, count, code = token & 15, (token >> 4) + 1, source.byte()
            if not 1 <= length <= 8 or count > 8 or code >= (1 << length):
                raise Invalid("invalid Nemesis table entry")
            key = format(code, "0{}b".format(length))
            for other in list(table) + ["111111"]:
                if key.startswith(other) or other.startswith(key):
                    raise Invalid("Nemesis table is not prefix-free or collides with inline marker")
            table[key] = (count, palette)
        bits, nibbles = Bits(source), []
        while len(nibbles) < required:
            key = ""
            for _ in range(8):
                key += str(bits.get(1))
                if key == "111111":
                    count, value = bits.get(3) + 1, bits.get(4)
                    break
                if key in table:
                    count, value = table[key]
                    break
            else:
                raise Invalid("Nemesis payload has no matching code")
            nibbles.extend([value] * count)
            event("nibble-run", count=count, value=value)
        if len(nibbles) != required:
            raise Invalid("Nemesis final run exceeds declared tile count")
        out.extend((nibbles[i] << 4) | nibbles[i + 1] for i in range(0, required, 2))
        if header & 32768:
            for i in range(4, len(out)):
                out[i] ^= out[i - 4]
    else:
        raise Invalid("unsupported audit codec: " + codec)
    return bytes(out), trace


def assess(codec, compressed, expected):
    try:
        decoded, trace = decode(codec, compressed)
        return {"syntax_valid": True, "matches_input": decoded == expected,
                "decoded_size": len(decoded), "decoded_sha256": hashlib.sha256(decoded).hexdigest(),
                "trace": trace if decoded != expected else []}
    except Invalid as error:
        return {"syntax_valid": False, "matches_input": False, "reason": str(error)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--comparison-dir", type=Path, action="append", default=[])
    parser.add_argument("--fixture-dir", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--summary", type=Path, help="Write a compact, reviewable verdict record")
    parser.add_argument("--decoder", nargs=2, action="append", default=[], metavar=("KIND", "DIRECTORY"))
    args = parser.parse_args()
    if args.decoder and not args.fixture_dir:
        parser.error("--decoder requires --fixture-dir")
    for kind, _ in args.decoder:
        if kind not in ("accurate-kosinski", "original-saxman", "clownlzss", "clownnemesis"):
            parser.error("Unknown decoder kind: " + kind)
    if os.name == "nt":
        import ctypes
        ctypes.windll.kernel32.SetErrorMode(0x8003)
    # Published Enigma example; Saxman example's prose has an endian typo.
    sample = bytes.fromhex("070c00000010053d118fe0")
    expected = bytes.fromhex("000000010010001000100010401840174016401540144013401240114010")
    if decode("enigma", sample)[0] != expected or decode("saxman", bytes.fromhex("03000000ff"))[0] != bytes(18):
        raise Invalid("published format example did not match")
    report = {"published_examples": 2, "fixtures": [], "comparisons": {}, "decoders": {}}
    if args.fixture_dir:
        for line in (args.fixture_dir / "index.tsv").read_text().splitlines():
            identifier, codec, _ = line.split("\t")
            if codec not in ("kosinski", "saxman", "enigma", "nemesis") or "headerless" in identifier:
                continue
            result = assess(codec, (args.fixture_dir / (identifier + ".compressed")).read_bytes(),
                            (args.fixture_dir / (identifier + ".raw")).read_bytes())
            report["fixtures"].append(dict(id=identifier, **result))
        for kind, directory in args.decoder:
            directory, rows = Path(directory), []
            for fixture in report["fixtures"]:
                identifier = fixture["id"]
                codec = identifier.split("/")[0]
                # Zero-tile data is a C++ empty-input contract, not an original-assembly oracle.
                if identifier == "nemesis/empty":
                    continue
                if kind == "accurate-kosinski" and codec == "kosinski":
                    command = [executable(directory, "kosinski-decompress")]
                elif kind == "original-saxman" and codec == "saxman":
                    command = [executable(directory, "original-saxman"), "decode"]
                elif kind == "clownnemesis" and codec == "nemesis":
                    command = [executable(directory, "clownnemesis-tool"), "-d"]
                elif kind == "clownlzss" and codec in ("kosinski", "saxman"):
                    command = [executable(directory, "clownlzss"), "-d", "-k" if codec == "kosinski" else "-s"]
                else:
                    continue
                if command[0] is None:
                    parser.error("Missing decoder for " + kind)
                output = args.output.parent / "decoder-checks" / kind / (identifier + ".bin")
                output.parent.mkdir(parents=True, exist_ok=True)
                output.unlink(missing_ok=True)
                source = (args.fixture_dir / (identifier + ".compressed")).resolve()
                result = run(command + [source, output.resolve()], output, 5)
                expected = (args.fixture_dir / (identifier + ".raw")).read_bytes()
                result["matches_expected"] = result.get("exit_code") == 0 and result.get("sha256") == hashlib.sha256(expected).hexdigest()
                rows.append(dict(id=identifier, **result))
            report["decoders"][kind] = rows
    for directory in args.comparison_dir:
        rows = []
        for row in json.loads((directory / "report.json").read_text())["cases"]:
            codec = row["codec"].lower()
            if codec not in ("kosinski", "saxman", "enigma", "nemesis"):
                continue
            root = directory / codec / row["case"]
            expected = (root / "input.bin").read_bytes()
            results = {"codec": codec, "case": row["case"]}
            results["expected_size"] = len(expected)
            results["cross_decodes"] = {key: row.get(key, {}).get("matches_input", False)
                                         for key in ("reference_to_reference", "reference_to_candidate", "candidate_to_reference", "candidate_to_candidate")}
            for producer in ("reference", "candidate"):
                path = root / (producer + ".compressed")
                results[producer] = assess(codec, path.read_bytes(), expected) if path.exists() else {"missing": True}
                results[producer]["encoder_process"] = row[producer + "_encode"]
                recorded = row[producer + "_encode"].get("sha256")
                if recorded and (not path.exists() or hashlib.sha256(path.read_bytes()).hexdigest() != recorded):
                    raise Invalid("Artifact differs from recorded hash: " + str(path))
            rows.append(results)
        report["comparisons"][directory.name] = rows
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + "\n")
    if args.summary:
        summary = {"schema_version": 1, "published_examples": report["published_examples"],
                   "format_models": ["kosinski", "saxman", "enigma", "nemesis"],
                   "model_fixture_cases": len(report["fixtures"]),
                   "model_fixture_failures": [r["id"] for r in report["fixtures"] if not r["matches_input"]],
                   "decoder_fixtures": report["decoders"], "comparisons": {}}
        for name, rows in report["comparisons"].items():
            codecs = {}
            for codec in sorted({r["codec"] for r in rows}):
                cases = [r for r in rows if r["codec"] == codec]
                failures = []
                for row in cases:
                    ref = row["reference"]
                    process = ref["encoder_process"]
                    if process.get("exit_code") != 0 or process.get("status") != "completed":
                        category = "reference encoder execution failure"
                    elif not ref.get("syntax_valid", False):
                        category = "reference encoder produced malformed stream"
                    elif not ref.get("matches_input", False):
                        category = "reference encoder produced wrong data under documented format"
                    elif not row["cross_decodes"]["reference_to_candidate"]:
                        category = "mdcomp decoder disagrees with format interpreter"
                    else:
                        continue
                    failures.append({"case": row["case"], "category": category,
                                     "reason": ref.get("reason"), "model_decoded_size": ref.get("decoded_size"),
                                     "expected_size": row["expected_size"]})
                codecs[codec] = {"cases": len(cases),
                                 "reference_encodings_match_format": sum(r["reference"].get("matches_input", False) for r in cases),
                                 "mdcomp_encodings_match_format": sum(r["candidate"].get("matches_input", False) for r in cases),
                                 "cross_decodes": {key: sum(r["cross_decodes"][key] for r in cases)
                                                   for key in cases[0]["cross_decodes"]},
                                 "encoding_failures_or_mdcomp_disagreements": failures}
            summary["comparisons"][name] = codecs
        args.summary.write_bytes((json.dumps(summary, indent=2) + "\n").encode("utf-8"))
    failed = [row["id"] for row in report["fixtures"] if not row["matches_input"]]
    print(json.dumps({"fixtures": len(report["fixtures"]), "fixture_failures": failed,
                      "decoder_fixtures": {kind: {"cases": len(rows), "passed": sum(r["matches_expected"] for r in rows)} for kind, rows in report["decoders"].items()},
                      "comparisons": {name: {p: sum(r[p].get("matches_input", False) for r in rows)
                                             for p in ("reference", "candidate")}
                                      for name, rows in report["comparisons"].items()}}))
    return bool(failed)


if __name__ == "__main__":
    raise SystemExit(main())
