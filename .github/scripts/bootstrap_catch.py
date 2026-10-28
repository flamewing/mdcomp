"""Build the pinned test dependency with the active target compiler."""
import argparse
import os
from pathlib import Path
import platform
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--work", default="build-deps")
    parser.add_argument("--generator", default="Ninja")
    parser.add_argument("--config", default="Release")
    args = parser.parse_args()
    work = Path(args.work)
    source = work / "Catch2"
    if not source.exists():
        subprocess.run(["git", "clone", "--branch", "v3.8.1", "--depth", "1",
                        "https://github.com/catchorg/Catch2.git", str(source)], check=True)
    revision = subprocess.check_output(["git", "-C", str(source), "rev-parse", "HEAD"], text=True).strip()
    if revision != "2b60af89e23d28eefc081bc930831ee9d45ea58b":
        raise RuntimeError("Catch2 checkout does not match the pinned v3.8.1 commit")
    configure = ["cmake", "-S", str(source), "-B", str(work / "catch-build"),
                    "-G", args.generator, "-DCATCH_BUILD_TESTING=OFF",
                    "-DCMAKE_BUILD_TYPE=" + args.config,
                    "-DCMAKE_INSTALL_PREFIX=" + str((work / "catch-install").resolve())]
    if platform.system() == "SunOS":
        # illumos math headers can expose an isnan macro before Catch declares
        # Catch::isnan. Let <cmath> consume/undefine it before those declarations.
        configure.append("-DCMAKE_CXX_FLAGS=" + os.environ.get("CXXFLAGS", "") + " -include cmath")
    subprocess.run(configure, check=True)
    subprocess.run(["cmake", "--build", str(work / "catch-build"), "--config",
                    args.config, "--parallel", "2"], check=True)
    subprocess.run(["cmake", "--install", str(work / "catch-build"), "--config",
                    args.config], check=True)


if __name__ == "__main__":
    main()
