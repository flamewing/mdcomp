# Running tests

Configure with CMake 3.30 or newer and an installed Catch2 v3 package. Catch2 is a test-only dependency; use `-DBUILD_TESTING=OFF` for a library/tool build without it. Tests are never installed or exported with mdcomp.

The [local baseline review](references/baseline.md) is the current record of compiler results, per-codec oracles, qualifications, and the remaining remote-CI gate. Earlier checkpoint counts below are retained as implementation history.

```sh
cmake -S . -B build-tests -DBUILD_TESTING=ON
cmake --build build-tests --parallel
ctest --test-dir build-tests --output-on-failure
ctest --test-dir build-tests -L unit --output-on-failure
```

For Visual Studio, add `--config Release` to the build and `-C Release` to CTest. To run a single case, use CTest's `-R` filter. Each unit case has a 30-second timeout and runs below the build directory. Constant-evaluated checks compile as part of the same unit target; runtime cases exercise the runtime implementations separately.

With vcpkg, select the manifest's test feature during configuration:

```sh
cmake -S . -B build-tests -DCMAKE_TOOLCHAIN_FILE=/path/to/vcpkg/scripts/buildsystems/vcpkg.cmake -DVCPKG_MANIFEST_FEATURES=tests -DBUILD_TESTING=ON
```

Other dependency managers can supply Catch2 through `CMAKE_PREFIX_PATH` or `Catch2_DIR`. For a reproducible standalone installation of the version used to bootstrap this suite:

```sh
git clone --branch v3.8.1 --depth 1 https://github.com/catchorg/Catch2.git build-deps/Catch2
cmake -S build-deps/Catch2 -B build-deps/catch-build -DCATCH_BUILD_TESTING=OFF -DCMAKE_INSTALL_PREFIX="$PWD/build-deps/catch-install"
cmake --build build-deps/catch-build --config Release --parallel
cmake --install build-deps/catch-build --config Release
cmake -S . -B build-tests -DBUILD_TESTING=ON -DCMAKE_PREFIX_PATH="$PWD/build-deps/catch-install"
```

The normal suite has 126 baseline CTest cases: 44 shared-header unit cases, 81 codec/fixture cases, and one installed-package case. A shared adapter exercises all ten basic codecs and all eight moduled APIs. The corpus includes deterministic descriptor-boundary inputs, padding policies, fixed-seed random bytes, seeded history, module boundaries, the largest legal module-frame size, invalid headers/padding, representable codec header limits, source offsets, and exact/short output buffers. Saxman sized/headerless streams and both Nemesis encoder modes have dedicated cases. Failure artifacts go below the test working directory under `failures/<codec>/<case>`.

Ninety-six hand-constructed vectors cover the initial declared decoder forms. Thirty-eight saved external encodings and 24 full-block/tail encodings add checks against qualified original KENS, KENSSharp, kirjavascript RLE, and Clownacy codec outputs. [Fixture documentation](fixtures/README.md) distinguishes their provenance and qualifications. [The low-level review](../docs/low-level-review.md) records fixes and remaining risks; [master-binary comparisons](references/README.md) and [external codec comparisons](references/external.md) document optional audits. [Format-based verdicts](references/format-verdicts.md) explain who is correct in the investigated disagreements and the target-specific Saxman rules. The [testing plan](../docs/testing-framework-plan.md) distinguishes completed initial edge/option coverage from exhaustive parameter sweeps, future checked-memory contracts, and remaining baseline qualifications.

Use `ctest --test-dir build-tests -L codec` for all codec cases, `-L known-answer` for saved/constructed vectors, `-L moduled` for module tests, `-L slow` for maximum legal frame sizes, and `-L external` for saved external encodings. Reference binary audits add optional tests labeled `reference`; exclude them with `-LE reference` when running the offline suite. The fixture manifest is checked through CMake in normal test runs, without a Python dependency.

The optional [assembly suite](asm/README.md) builds pinned ASL with M68K/Z80 backends and runs direct decoders under bounded emulators. Enable it with `-DMDCOMP_ASSEMBLY_TESTS=ON` after initializing submodules; the README also describes explicitly importing four original-derived Sega decoders. The suite adds eight CTest groups (`-L asm`) to the ordinary 126 entries, covering 4,340 direct decoder/case checks across 52 images and 1,168 moduled cases across eight queue images, plus calibration/CLI checks. `-DMDCOMP_ASSEMBLY_VARIANTS=OFF` retains defaults and the four headerless SaxDec2 entries. Its `mdcomp-asm-run` executable decompresses supplied files and reports 68000 clock/modeled-bus counts or Z80 T-states; JSON output and replacement ASL images support optimization experiments. The [moduled hardware contract](asm/moduled-hardware.md) qualifies the DMA/interrupt shim; configurations outside the declared matrix and real-hardware confirmation remain pending.

Bit reads/writes accept counts from zero through the word width. Zero counts leave the buffer unchanged; larger counts throw `std::out_of_range`. Callback construction and stream errors propagate. LZSS writers provide an explicit, idempotent `finish()` for final write failures; their noexcept destructor fallback records stream failure instead of throwing during cleanup.

Endian stream operations preserve ordinary `istream`/`ostream` state and exception-mask behavior. Direct `streambuf` transfers throw `std::ios_base::failure` when `sgetn` or `sputn` transfers fewer bytes than requested; an underlying exception propagates. A failed transfer can consume or produce a prefix and does not roll back. Endian input iterators yield complete values only, read their first value during construction, and become end iterators on an incomplete read when the stream does not throw.

`MDCOMP_HAS_INT128` indicates availability of `utils::int128_t`, `utils::uint128_t`, and the `read16`/`write16` entry points. Typed I/O, `read_n<16>`/`write_n<16>`, and byte swapping also accept these types. GCC and Clang use native 128-bit integers, including in strict C++20 mode. Microsoft STL builds without native 128-bit integers use its internal `std::_Signed128` and `std::_Unsigned128` classes when `<__msvc_int128.hpp>` is available; these are implementation-specific types, so that path is checked by the MSVC CI job. Other integer types retain the standard integral concept checks.

Iterator and container transfers accept `char`, `signed char`, and `unsigned char` elements. Wider elements and `bool` are rejected. Custom output iterators must expose a byte-valued `value_type`; standard insertion and ostream iterators are recognized separately because their standard `value_type` is `void`. Scalar transfers still accept an unsigned scalar of the same width as the value being transferred.

The `[edges]` cases reconstruct compressed streams through a separate test-only interpreter and require every obligation in [edges.json](fixtures/edges.json). Run `ctest --test-dir build-tests -L edges --output-on-failure` and inspect `tests/encoder-edges.tsv` and `tests/decoder-edges.tsv` under the build directory for the first validated witness of each form. These checks detected erased supplemental matches in the shared LZSS search, a wrong packed-literal source pointer in LZKN1, and its unintentionally restricted packed length. Their regression tests validate emitted tokens and reconstructed bytes.

On Linux, enable AddressSanitizer and UndefinedBehaviorSanitizer for the C++ codecs, harness, and C emulator cores with the following configuration. ASL remains a separate uninstrumented host-tool build.

```sh
cmake -S . -B build-sanitize -G Ninja -DBUILD_TESTING=ON -DMDCOMP_ASSEMBLY_TESTS=ON -DCMAKE_BUILD_TYPE=Debug -DCMAKE_C_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer" -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer"
cmake --build build-sanitize --parallel
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 ctest --test-dir build-sanitize --output-on-failure
```

The local Ubuntu/WSL GCC 15.2 build passed all 123 tests with both sanitizers, including imported originals and the complete assembly matrix. Its TSV results match the three Windows compiler builds after normalizing line endings. The Linux Clang/Ninja CI job now enables the same instrumentation and preserves edge reports and CTest diagnostics on failure; remote CI results have not been observed in this session.

Module wire checks independently reconstruct each payload, require the correct per-module output size and fresh history, and verify zero padding relative to the payload origin for alignments 1, 3, 16, and 32. Saved qualified external bodies also run as short final modules, retaining their original compressed bytes and provenance. The latest full matrix passes 125 CTest entries on GCC, Clang, and Linux ASan/UBSan, and 126 on MSVC with its optional binary comparison.

The `package::installed-codecs` case installs into a fresh directory under the build tree, configures a separate consumer with package discovery restricted to that install, links all ten static codecs through exported targets, and runs them without Catch2. It caught the missing installed CMake export and missing transitive Boost header requirement. Run it with `ctest --test-dir build-tests -L package --output-on-failure` after building all targets. Full local suites now pass 126 entries on GCC, Clang, and Linux ASan/UBSan, and 127 on MSVC including the binary comparison.

[Test contribution and TDD instructions](CONTRIBUTING.md) describe the required failing-fixture, wire-observer, provenance, and regression workflow, with copyable adapter/fixture pointers and commands for one codec, fixture, or architecture. The optional `MDCOMP_STRESS_TESTS` sweep adds one CTest case containing 2,560 fixed-seed comparisons; it passes on all four local configurations. Weekly/manual Linux CI enables it with sanitizers and uses a compiler cache for project and pinned host-tool compilation. CTest commands in CI now fail on an empty selection. Workflow YAML was parsed locally; remote execution and cache reuse remain unobserved.

Twenty-four additional qualified external encodings supply paired 4,096-byte blocks and 64-byte tails for six available reference-codec families. They are checked directly, through independent wire observers, and as canonical two-full-module-plus-tail archives. Applicable archives run through the assembly DMA/interrupt shim. Their separate manifest is hash-checked by normal CTest. Current full local runs contain 129 tests with the optional stress sweep, or 130 on MSVC with the additional binary comparison.

The subsequent header-limit, output-failure, and input-position regressions bring the current full suite to 134 cases with assembly and stress enabled, or 135 on MSVC with the binary comparison. All pass on GCC, Clang, MSVC, and Linux ASan/UBSan. The package check now builds separate static and shared consumers; Windows also verifies that every codec DLL is loaded. It exposed missing MSVC exports/import-library installation and colliding static/import filenames, now fixed while preserving the existing static filenames and CMake target names.

Optional [compiler coverage](references/coverage.md) runs through the manual Linux workflow input and uploads diagnostic line/branch measurements. It complements the format-edge reports without replacing their semantic obligations or imposing a compiler-dependent percentage gate.

The expanded-platform follow-up reproduces and fixes two libc++ exception-test assumptions and adds a runtime 128-bit source-preservation regression exposed by MSVC Win32. The current ordinary count is 126 cases, or 134 with assembly and 135 with stress; the optional binary comparison adds one more. GCC Windows, MSVC Win32, and local Linux libc++ qualification pass their enabled suites. See [the CI follow-up record](../.github/CI.md#first-expanded-matrix-results) for the remote results and remaining reruns.
