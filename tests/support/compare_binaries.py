"""Compare pinned mdcomp CLI builds without assuming either encoder is an oracle."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import time

TOOLS = {
    "comper": "compcmp", "comperx": "comperx", "enigma": "enicmp",
    "kosinski": "koscmp", "kosplus": "kosplus", "lzkn1": "lzkn1cmp",
    "nemesis": "nemcmp", "rocket": "rockcmp", "saxman": "saxcmp", "snkrle": "snkcmp",
}


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def executable(directory, name):
    path = directory / (name + (".exe" if os.name == "nt" else ""))
    return path if path.is_file() else None


def corpus():
    for size in (32, 64, 256, 512):
        state = 0x12345678
        random_bytes = bytearray()
        for _ in range(size):
            state ^= (state << 13) & 0xffffffff
            state ^= state >> 17
            state ^= (state << 5) & 0xffffffff
            random_bytes.append(state & 0xff)
        for name, data in (
                ("zeros", bytes(size)), ("repeat", b"A" * size),
                ("ascending", bytes(i % 256 for i in range(size))),
                ("random-seed-12345678", bytes(random_bytes))):
            yield "{}-{}".format(name, size), data


def run(command, output, timeout, runtime_dirs=()):
    log = output.with_suffix(output.suffix + ".log")
    start = time.monotonic()
    flags = getattr(subprocess, "CREATE_NO_WINDOW", 0)
    environment = os.environ.copy()
    if runtime_dirs:
        environment["PATH"] = os.pathsep.join(str(p.resolve()) for p in runtime_dirs) + os.pathsep + environment.get("PATH", "")
    with log.open("wb") as stream:
        try:
            process = subprocess.Popen([str(x) for x in command], stdout=stream,
                                       stderr=subprocess.STDOUT, creationflags=flags, env=environment)
        except OSError as error:
            return {"status": "launch-error", "error": str(error)}
        status = "completed"
        while process.poll() is None:
            if time.monotonic() - start > timeout:
                status = "timeout"
            elif output.exists() and output.stat().st_size > 1024 * 1024:
                status = "output-limit"
            if status != "completed":
                process.kill()
                break
            time.sleep(0.01)
        code = process.wait()
    result = {"status": status, "exit_code": code}
    if os.name == "nt" and (code & 0xffffffff) == 0xc0000135:
        result["status"] = "missing-runtime"
        result["diagnostic"] = "Windows could not load a dependent DLL; supply the tool's runtime directory."
    if output.is_file() and output.stat().st_size <= 1024 * 1024:
        result.update(size=output.stat().st_size, sha256=digest(output))
    if code != 0 or status != "completed":
        result.setdefault("diagnostic", log.read_bytes()[:2048].decode("utf-8", errors="replace"))
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--reference-dir", type=Path, required=True)
    parser.add_argument("--candidate-dir", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--reference-commit", default="unspecified")
    parser.add_argument("--candidate-commit", default="unspecified")
    parser.add_argument("--reference-runtime-dir", type=Path, action="append", default=[])
    parser.add_argument("--candidate-runtime-dir", type=Path, action="append", default=[])
    parser.add_argument("--timeout", type=float, default=5)
    args = parser.parse_args()
    if os.name == "nt":
        import ctypes
        # Children inherit this mode: loader/crash failures must become report
        # entries, never interactive dialogs on the user's desktop or CI runner.
        ctypes.windll.kernel32.SetErrorMode(0x0001 | 0x0002 | 0x8000)
    args.output_dir.mkdir(parents=True, exist_ok=True)
    report = {"reference_commit": args.reference_commit,
              "candidate_commit": args.candidate_commit,
              "reference_runtime_dirs": [str(p.resolve()) for p in args.reference_runtime_dir],
              "candidate_runtime_dirs": [str(p.resolve()) for p in args.candidate_runtime_dir],
              "classification": "shared-lineage cross-version regression comparison",
              "cases": [], "executables": {}, "missing": []}
    for codec, name in TOOLS.items():
        reference = executable(args.reference_dir.resolve(), name)
        candidate = executable(args.candidate_dir.resolve(), name)
        if not reference or not candidate:
            report["missing"].append(codec)
            continue
        report["executables"][codec] = {
            "reference_sha256": digest(reference), "candidate_sha256": digest(candidate)}
        for case, data in corpus():
            directory = args.output_dir / codec / case
            directory.mkdir(parents=True, exist_ok=True)
            raw = directory / "input.bin"
            raw.write_bytes(data)
            encoded = {}
            row = {"codec": codec, "case": case, "input_sha256": digest(raw)}
            for label, tool, runtime in (("reference", reference, args.reference_runtime_dir),
                                         ("candidate", candidate, args.candidate_runtime_dir)):
                path = directory / (label + ".compressed")
                if path.exists():
                    path.unlink()
                row[label + "_encode"] = run([tool, raw, path], path, args.timeout, runtime)
                if row[label + "_encode"]["status"] in ("missing-runtime", "launch-error"):
                    report["setup_failure"] = {"tool": str(tool), "result": row[label + "_encode"]}
                    report["summary"] = {"status": "incomplete", "completed_cases": len(report["cases"])}
                    (args.output_dir / "report.json").write_text(json.dumps(report, indent=2, sort_keys=True) + "\n")
                    print("Cannot launch reference/candidate tool; see report.json", flush=True)
                    return 2
                encoded[label] = path
            for producer in ("reference", "candidate"):
                encoding = row[producer + "_encode"]
                if encoding.get("exit_code") != 0 or "sha256" not in encoding:
                    continue
                for consumer, tool, runtime in (("reference", reference, args.reference_runtime_dir),
                                               ("candidate", candidate, args.candidate_runtime_dir)):
                    path = directory / (producer + "-decoded-by-" + consumer + ".bin")
                    if path.exists():
                        path.unlink()
                    result = run([tool, "-x", encoded[producer], path], path, args.timeout, runtime)
                    result["matches_input"] = (result.get("exit_code") == 0
                                               and result.get("sha256") == row["input_sha256"])
                    row[producer + "_to_" + consumer] = result
            row["reference_valid"] = row.get("reference_to_reference", {}).get("matches_input", False)
            row["candidate_valid"] = row.get("candidate_to_candidate", {}).get("matches_input", False)
            row["encoded_equal"] = (row["reference_encode"].get("sha256") is not None
                                      and row["reference_encode"].get("sha256")
                                      == row["candidate_encode"].get("sha256"))
            row["discrepancy"] = row["reference_valid"] and (
                not row["candidate_valid"]
                or not row.get("reference_to_candidate", {}).get("matches_input", False)
                or not row.get("candidate_to_reference", {}).get("matches_input", False))
            row["size_regression"] = (row["reference_valid"] and row["candidate_valid"]
                                      and row["candidate_encode"]["size"] > row["reference_encode"]["size"])
            report["cases"].append(row)
    rows = report["cases"]
    report["summary"] = {
        "cases": len(rows), "discrepancies": sum(r["discrepancy"] for r in rows),
        "reference_failures": sum(not r["reference_valid"] for r in rows),
        "candidate_failures": sum(not r["candidate_valid"] for r in rows),
        "encoded_differences": sum(not r["encoded_equal"] for r in rows),
        "size_regressions": sum(r["size_regression"] for r in rows),
    }
    (args.output_dir / "report.json").write_text(json.dumps(report, indent=2, sort_keys=True) + "\n")
    print(json.dumps(report["summary"], sort_keys=True), flush=True)
    return 1 if (report["summary"]["discrepancies"] or report["summary"]["size_regressions"] or report["summary"]["reference_failures"]
                 or report["summary"]["candidate_failures"] or not rows or report["missing"]) else 0


if __name__ == "__main__":
    raise SystemExit(main())
