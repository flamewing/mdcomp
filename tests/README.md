# Running tests

Configure with CMake 3.30 or newer and an installed Catch2 v3 package. Catch2 is a test-only dependency; use `-DBUILD_TESTING=OFF` for a library/tool build without it. Tests are never installed or exported with mdcomp.

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

The normal suite has 115 CTest cases: 43 shared-header unit cases and 72 codec/fixture cases. A shared adapter exercises all ten basic codecs and all eight moduled APIs. The corpus includes deterministic descriptor-boundary inputs, padding policies, fixed-seed random bytes, seeded history, module boundaries, the largest legal module-frame size, and invalid headers/padding. Saxman sized/headerless streams and both Nemesis encoder modes have dedicated cases. Failure artifacts go below the test working directory under `failures/<codec>/<case>`.

Ninety-four hand-constructed vectors cover the initial declared decoder forms. Thirty-eight saved external encodings add checks against qualified original KENS, KENSSharp, kirjavascript RLE, and Clownacy codec outputs. [Fixture documentation](fixtures/README.md) distinguishes their provenance and qualifications. [The low-level review](../docs/low-level-review.md) records fixes and remaining risks; [master-binary comparisons](references/README.md) and [external codec comparisons](references/external.md) document optional audits. [Format-based verdicts](references/format-verdicts.md) explain who is correct in the investigated disagreements, the target-specific Saxman rules, and how to run the format interpreters/direct decoder checks. Complete edge inventories, every encoder option, encoder-selection observers, and additional assembly configurations remain tracked in [the testing plan](../docs/testing-framework-plan.md); the pre-refactor baseline gate remains blocked.

Use `ctest --test-dir build-tests -L codec` for all codec cases, `-L known-answer` for saved/constructed vectors, `-L moduled` for module tests, `-L slow` for maximum legal frame sizes, and `-L external` for saved external encodings. Reference binary audits add optional tests labeled `reference`; exclude them with `-LE reference` when running the offline suite. The fixture manifest is checked through CMake in normal test runs, without a Python dependency.

The optional [assembly suite](asm/README.md) builds pinned ASL with M68K/Z80 backends and runs direct decoders under bounded emulators. Enable it with `-DMDCOMP_ASSEMBLY_TESTS=ON` after initializing submodules; the README also describes explicitly importing four original-derived Sega decoders. The suite adds eight CTest groups (`-L asm`) to the ordinary 115 entries, covering 4,124 direct decoder/case checks across 52 images and 1,112 moduled cases across eight queue images, plus calibration/CLI checks. `-DMDCOMP_ASSEMBLY_VARIANTS=OFF` retains defaults and the four headerless SaxDec2 entries. Its `mdcomp-asm-run` executable decompresses supplied files and reports 68000 clock/modeled-bus counts or Z80 T-states; JSON output and replacement ASL images support optimization experiments. The [moduled hardware contract](asm/moduled-hardware.md) qualifies the DMA/interrupt shim; configurations outside the declared matrix and real-hardware confirmation remain pending.

Bit reads/writes accept counts from zero through the word width. Zero counts leave the buffer unchanged; larger counts throw `std::out_of_range`. Callback construction and stream errors propagate. LZSS writers provide an explicit, idempotent `finish()` for final write failures; their noexcept destructor fallback records stream failure instead of throwing during cleanup.

Endian stream operations preserve ordinary `istream`/`ostream` state and exception-mask behavior. Direct `streambuf` transfers throw `std::ios_base::failure` when `sgetn` or `sputn` transfers fewer bytes than requested; an underlying exception propagates. A failed transfer can consume or produce a prefix and does not roll back. Endian input iterators yield complete values only, read their first value during construction, and become end iterators on an incomplete read when the stream does not throw.

`MDCOMP_HAS_INT128` indicates availability of `utils::int128_t`, `utils::uint128_t`, and the `read16`/`write16` entry points. Typed I/O, `read_n<16>`/`write_n<16>`, and byte swapping also accept these types. GCC and Clang use native 128-bit integers, including in strict C++20 mode. Microsoft STL builds without native 128-bit integers use its internal `std::_Signed128` and `std::_Unsigned128` classes when `<__msvc_int128.hpp>` is available; these are implementation-specific types, so that path is checked by the MSVC CI job. Other integer types retain the standard integral concept checks.

Iterator and container transfers accept `char`, `signed char`, and `unsigned char` elements. Wider elements and `bool` are rejected. Custom output iterators must expose a byte-valued `value_type`; standard insertion and ostream iterators are recognized separately because their standard `value_type` is `void`. Scalar transfers still accept an unsigned scalar of the same width as the value being transferred.

The `[edges]` cases reconstruct compressed streams through a separate test-only interpreter and require every obligation in [edges.json](fixtures/edges.json). Run `ctest --test-dir build-tests -L edges --output-on-failure` and inspect `tests/encoder-edges.tsv` and `tests/decoder-edges.tsv` under the build directory for the first validated witness of each form. These checks detected erased supplemental matches in the shared LZSS search, a wrong packed-literal source pointer in LZKN1, and its unintentionally restricted packed length. Their regression tests validate emitted tokens and reconstructed bytes.
