"""Package a staged installation, including non-system C++ runtime dependencies."""
import argparse
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import zipfile

ROOT = Path(__file__).resolve().parents[2]
TOOLS = ("compcmp", "comperx", "enicmp", "koscmp", "kosplus", "lzkn1cmp",
         "nemcmp", "rockcmp", "saxcmp", "snkcmp")


def command(*args):
    return subprocess.check_output(args, text=True)


def windows_dependencies(text):
    dependencies = []
    for line in text.splitlines():
        if not line.strip():
            continue
        missing = re.fullmatch(r"\s*(.+?)\s+=>\s+not found\s*", line)
        resolved = re.fullmatch(r"\s*(.+?)\s+=>\s+(.+?)\s+\(0x[0-9a-fA-F]+\)\s*", line)
        if missing:
            dependencies.append((missing[1], None))
        elif resolved:
            dependencies.append((resolved[1], resolved[2]))
        else:
            raise RuntimeError("Unrecognized ntldd output: " + line)
    return dependencies


def windows_runtime(stage, package_target=None):
    pending = list((stage / "bin").glob("*.exe")) + list((stage / "bin").glob("*.dll"))
    searched = set()
    system = Path(os.environ["SystemRoot"]).resolve()
    while pending:
        binary = pending.pop()
        if binary.name.lower() in searched:
            continue
        searched.add(binary.name.lower())
        # Traverse application DLLs ourselves so optional dependencies inside
        # Windows system libraries do not enter the package dependency graph.
        scanner = ["ntldd", "-D", str(stage / "bin")]
        if package_target == "windows-i686" and (system / "SysWOW64").is_dir():
            # A 64-bit ntldd can inspect x86 PE files, but needs the x86
            # Windows DLL directory when resolving their system imports.
            scanner += ["-D", str(system / "SysWOW64")]
        output = command(*scanner, "--", str(binary))
        for name, resolved in windows_dependencies(output):
            if name.lower().startswith(("api-ms-", "ext-ms-")):
                continue
            if name.lower() in ("msys-2.0.dll", "cygwin1.dll"):
                raise RuntimeError("Native package depends on a POSIX emulation DLL: " + name)
            if resolved is None:
                raise RuntimeError("Cannot locate runtime dependency: " + name)
            found = Path(resolved)
            if system in found.resolve().parents:
                continue
            if not found.is_file():
                raise RuntimeError("ntldd resolved a missing runtime: " + resolved)
            target = stage / "bin" / name
            if not target.exists():
                shutil.copy2(found, target)
            pending.append(target)


def linux_runtime(stage):
    # ldd is used only on our own native binaries, never downloaded executables.
    for binary in (stage / "bin").iterdir():
        dependencies = command("ldd", str(binary))
        if "not found" in dependencies:
            raise RuntimeError("Unresolved installed dependencies: " + dependencies)
        for name, path in re.findall(r"\s*(\S+) => (/\S+) \(", dependencies):
            if name.startswith(("libstdc++.", "libgcc_s.", "libc++.", "libc++abi.", "libunwind.")):
                target = stage / "lib" / name
                if not target.exists():
                    shutil.copy2(path, target)
                    subprocess.run(["patchelf", "--set-rpath", "$ORIGIN", str(target)], check=True)


def macos_runtime(stage):
    compiler = Path(os.environ["CXX"]).resolve().parent.parent
    search = [stage / "lib", compiler / "lib/c++", compiler / "lib/unwind", compiler / "lib"]
    pending = list((stage / "bin").iterdir()) + list((stage / "lib").glob("*.dylib"))
    visited = set()
    while pending:
        binary = pending.pop()
        if binary in visited:
            continue
        visited.add(binary)
        for line in command("otool", "-L", str(binary)).splitlines()[1:]:
            dependency = line.strip().split(" (", 1)[0]
            if dependency.startswith(("/usr/lib/", "/System/Library/")):
                continue
            name = Path(dependency).name
            found = Path(dependency)
            if not found.is_file():
                found = next((p / name for p in search if (p / name).is_file()), None)
            if found is None:
                raise RuntimeError("Cannot locate dylib: " + dependency)
            target = stage / "lib" / name
            if not target.exists():
                shutil.copy2(found, target)
            subprocess.run(["install_name_tool", "-change", dependency,
                            "@loader_path/../lib/" + name, str(binary)], check=True)
            pending.append(target)
        if binary.suffix == ".dylib":
            subprocess.run(["install_name_tool", "-id", "@rpath/" + binary.name,
                            str(binary)], check=True)
    # Changing load commands invalidates Mach-O signatures, including on ARM64.
    for binary in sorted(visited):
        subprocess.run(["codesign", "--force", "--sign", "-", str(binary)], check=True)


def package(stage, output, build_id, target, bundle=True):
    if not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9_.+-]*", build_id):
        raise ValueError("Invalid build identifier")
    catalog = json.loads((ROOT / ".github/packages.json").read_text())
    allowed = {item["id"] for item in catalog["native"]} | set(catalog["required_vm"] + catalog["required_msvc"] + catalog["optional"])
    if target not in allowed:
        raise ValueError("Unknown package target")
    suffix = ".exe" if target.startswith("windows-") else ""
    for tool in TOOLS:
        if not (stage / "bin" / (tool + suffix)).is_file():
            raise RuntimeError("Missing packaged tool: " + tool)
    if bundle:
        if target.startswith("windows-"):
            windows_runtime(stage, target)
        elif target.startswith("linux-"):
            linux_runtime(stage)
        elif target.startswith("macos-"):
            macos_runtime(stage)
        elif target.startswith("android-"):
            runtime = Path(os.environ["MDCOMP_NDK"]) / "toolchains/llvm/prebuilt/linux-x86_64/sysroot/usr/lib/aarch64-linux-android/libc++_shared.so"
            shutil.copy2(runtime, stage / "lib/libc++_shared.so")
    for name in ("COPYING.md", "COPYING-asm.md", "README.md"):
        shutil.copy2(ROOT / name, stage / name)
    (stage / "BUILD-INFO.json").write_text(json.dumps({
        "build_id": build_id, "target": target, "commit": os.environ.get("RELEASE_SHA", os.environ.get("GITHUB_SHA", "unknown")),
        "source": "https://github.com/flamewing/mdcomp", "cross_compiled": target.startswith("android-")
    }, indent=2) + "\n", encoding="utf-8")
    output.mkdir(parents=True, exist_ok=True)
    archive = output / f"mdcomp-{target}-{build_id}.zip"
    with zipfile.ZipFile(archive, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as packed:
        for path in sorted(stage.rglob("*")):
            if path.is_file():
                # Dereference library symlinks for cross-platform ZIP extraction.
                packed.write(path, path.relative_to(stage))
    return archive


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--stage", type=Path, required=True)
    parser.add_argument("--output", type=Path, default=Path("release-assets"))
    parser.add_argument("--build-id", required=True)
    parser.add_argument("--target", required=True)
    args = parser.parse_args()
    print(package(args.stage.resolve(), args.output, args.build_id, args.target))


if __name__ == "__main__":
    main()
