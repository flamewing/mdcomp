# CI, snapshots, and releases

The workflow structure follows the pinned ASL reference under `tests/third_party/asl/.github`: explicit permissions, pinned actions, independent matrix failures, bounded execution, complete failure artifacts, native architecture coverage, and publishing owned drafts only after all required packages are ready. Dependabot groups action-pin updates weekly. Compiler/test caches do not replace pinned source inputs.

## Continuous integration

| Workflow | Coverage |
| --- | --- |
| `ci-linux.yml` | GCC on x86-64 and ARM64, a Makefiles generator check, Clang 19 on both architectures, ASan/UBSan on x86-64, weekly/manual stress, optional manual GCC coverage |
| `ci-macos.yml` | Homebrew LLVM/libc++, Intel and Apple Silicon |
| `ci-windows-mingw.yml` | MINGW32 and UCRT64, Ninja; the deprecated 32-bit environment remains while its toolchain is available |
| `ci-windows-msvc.yml` | VS2022 and the latest installed VS generator, each Debug/Release; pinned vcpkg and the published-binary comparison |
| `ci-freebsd.yml` | Native build and C++/assembly tests inside a FreeBSD VM |
| `ci-omnios.yml` | GCC 13 native build and C++/assembly tests inside an OmniOS VM; hash-pinned Boost headers where system packaging is unavailable |
| `ci-android.yml` | NDK ARM64/API 24 cross build; no device or emulator execution claim |
| `ci-emscripten.yml` | Pinned emsdk cross build and ten command-line round trips under Node |

Native jobs enable the pinned ASL/emulator suite and explicitly import hash-verified original-derived Sega sources. Cross builds do not attempt to run host-only assembly fixtures. Catch2 v3.8.1 is built with the selected compiler, or supplied by the selected MinGW environment/vcpkg; tests remain optional for library consumers. CMake 3.30 is the actual minimum required by the existing policy and test integration. Native jobs pin CMake 4.2.3/Ninja 1.13.2 where the runner's system tooling may be older; MinGW and VM jobs use their package managers.

Shared CI scripts and workflows trigger the native checks. Documentation-only changes are excluded. CI has read-only repository access, no persisted checkout credentials, job timeouts, and failure artifacts retained for seven days. Source and package archives exclude test dependencies. See [the test contribution workflow](../tests/CONTRIBUTING.md) for local test commands and [compiler coverage](../tests/references/coverage.md) for diagnostic measurement.

## Packaging and publication

`.github/packages.json` defines seven hosted native targets, required FreeBSD, and optional Android. `package-targets.yml` builds the same package set for snapshots and stable releases. Native builds must pass CTest and staged CLI smoke checks. Android is a nonblocking cross build and carries its shared C++ runtime; its build metadata records that it is cross-compiled. There is no WebAssembly or OmniOS release asset in this initial ASL-aligned snapshot matrix.

The staged installation contains all ten working command-line codecs, libraries, installed headers/CMake exports, licenses, and `BUILD-INFO.json` with the exact commit/target/build ID. Windows imports are inspected with `objdump`, and non-system DLLs are copied recursively; packaging rejects accidental MSYS/Cygwin dependencies. Linux C++ runtimes are copied with an origin-relative search path. macOS dylib paths are relocated and modified Mach-O files are signed ad hoc; these are not Apple-notarized packages. Packages target the corresponding runner OS baseline, rather than every historical version of that OS.

`snapshots.yml` runs daily at 01:00 UTC or manually. Scheduled runs skip a commit only when this workflow already completed successfully for the same branch/SHA; a failed attempt remains eligible for the next schedule. Snapshot tags include the project version, UTC run-creation date, and run ID, so retries across midnight retain their identity. Snapshots are prereleases, leaving GitHub's latest stable release separate.

`release.yml` builds on a pushed `v<version>` tag or a manual run naming an existing tag. The tag must match the CMake/vcpkg version, and its resolved commit must match the checked-out source. Annotated tags are resolved and checked too. Neither workflow uses `pull_request_target`. Build jobs have read-only permissions; only preparation/publication jobs receive repository-write permissions. No release or tag is created merely by running local tests.

`prepare_release.py` and `release.py` record a run/SHA ownership marker in a draft. `finish-release.yml` downloads packages from that same workflow run, validates ZIP integrity and metadata, writes `SHA256SUMS`, uploads the complete asset set, and publishes only after every required native package and checksum file is present. A rerun reuses its owned draft, or recreates a draft cleaned up by its previous failed attempt. A published release is left unchanged. A failed build/upload/publish can delete only its own validated draft by recorded ID; tags and unrelated/published releases are never deleted. Cancellation leaves the draft available for investigation or retry. Publishing runs are serialized rather than cancelled midway.

VirusTotal scanning is optional when `VT_API_KEY` is configured. Automated builds scan local archives without making scanner availability a publication gate. `released.yml` retains the separate scan/body-link update for manually published GitHub releases. GitHub-token-created releases do not retrigger that event workflow. Coverity retains its existing secrets and schedule, compares against the last successful scan SHA, disables test dependencies during capture, and verifies an accepted server response before reporting success.

## Validation and limitations

Run `python3 -B -m unittest discover -s .github/scripts/tests -v` for offline publishing/package checks. They do not need credentials or contact GitHub. Run actionlint on the workflow files to check syntax, expressions, action inputs, and reusable-workflow wiring. The release and snapshot scripts should be reviewed together with the package catalog when changing the required matrix.

The local validation of this update includes the passing 134-case Windows native suite, a WebAssembly build and all ten Node CLI round trips, Linux packaging with relocated C++ runtimes, Windows packaging with development directories removed from PATH, all ten packaged CLI round trips on both hosts, 21 offline release/package/documentation tests on Windows and Linux, and actionlint with ShellCheck. Both real package ZIPs also pass publisher integrity/provenance validation without performing an upload. Newly added remote architectures, VMs, NDK builds, action downloads, Coverity submissions, VirusTotal requests, and publication still require their own remote qualification. Earlier [baseline CI results](../tests/references/baseline.md) describe the preceding workflow revision; they do not establish that every new matrix entry has run.
