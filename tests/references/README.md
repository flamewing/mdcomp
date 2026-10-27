# Pinned master-binary comparisons

For original KENS, KENSSharp, and kirjavascript RLE comparisons, see [external codec qualification](external.md). Those references have separate pins, lineage notes, reported failures, and saved fixtures.

The reference tools share mdcomp ancestry. They are cross-version regression references, not independent format oracles. No ROM assets are used. [master.json](master.json) pins source/release commits, the archive URL and SHA-256, executable hashes, compiler details, and the deterministic corpus. Binaries and generated comparison artifacts stay in ignored build directories.

Two references are retained: the published `snapshot-v0.2.0.20211206` Windows binaries targeting `83ddc58527637d193b883606f8b94625574d2e0c`, and an isolated source build of master at `72c6df405a75d322c5b3722da46c3abb864d3793`. The pre-test branch is pinned at `a8b9a26`, immediately before the Catch2 infrastructure commit. The published binaries can be imported explicitly; normal configure/build/test runs never download them.

```sh
python tests/support/import_reference.py --destination build-reference/published
```

For offline import, append `--archive /path/to/mdcomp-v0.2.0.20211206.zip`. The importer checks the archive hash, validates extraction paths, and checks every executable hash. The published tools require Windows; source-built references can be native to another host.

Build frozen master without switching the working branch:

```sh
cmake -E make_directory build-reference/master-source
git archive 72c6df405a75d322c5b3722da46c3abb864d3793 -o build-reference/master.tar
tar -xf build-reference/master.tar -C build-reference/master-source
cmake -S build-reference/master-source -B build-reference/master-build -DCMAKE_BUILD_TYPE=Release -DCMAKE_POLICY_DEFAULT_CMP0167=NEW
cmake --build build-reference/master-build --parallel
```

Run the comparison explicitly, selecting directories containing the CLI executables and their runtime libraries:

```sh
python tests/support/compare_binaries.py --reference-dir build-reference/master-build --reference-commit 72c6df405a75d322c5b3722da46c3abb864d3793 --candidate-dir build-tests --candidate-commit YOUR_COMMIT --output-dir build-reference/comparison
```

With Visual Studio, point `--candidate-dir` at the chosen configuration directory, such as `build-tests/Release`. For a build without vcpkg's app-local DLL deployment, add `--candidate-runtime-dir /path/to/vcpkg_installed/x64-windows/bin` so the tools can load `getopt.dll`. The corresponding reference option is `--reference-runtime-dir`; both can be repeated. CTest supplies the imported MSVC getopt runtime directory automatically. The harness disables Windows loader/crash dialogs and records missing-runtime failures. Each command has a timeout and output-size limit. Inputs, both encodings, all self/cross-decoded outputs, logs, and a JSON report remain under `--output-dir`. Reports record executable and input/output hashes. Missing tools, invalid self-checks, cross-decoding discrepancies, and larger candidate encodings return failure; different but equally small valid encodings are reported separately.

CTest integration is opt-in:

```sh
cmake -S . -B build-tests -DMDCOMP_REFERENCE_BIN_DIR=/absolute/path/to/reference-tools -DMDCOMP_REFERENCE_COMMIT=72c6df405a75d322c5b3722da46c3abb864d3793
cmake --build build-tests --parallel
ctest --test-dir build-tests -L reference --output-on-failure
```

The matrix contains 160 cases per comparison: ten codecs, four input sizes (32, 64, 256, 512 bytes), and zero, repeated-byte, ascending-byte, and fixed-seed xorshift32 patterns. Both tools encode each input, each decoder reads its own output, and each decoder reads the other encoder's output. Input sizes are multiples of 32 so they satisfy word/tile granularity without relying on padding policy. Moduled and non-default CLI options are not yet included.

## Findings

Both references pass all 160 self-checks. The pre-test branch failed 25 cases against each reference: nine ComperX cases produced incorrect output and all 16 Rocket encodes crashed. Enigma had seven byte differences, including four compressed-size regressions of two bytes. The Rocket crash initially hid a separate four-byte compressed-header length error, which the published decoder exposed after fill was repaired.

At the earlier repaired checkpoint, the branch matched both references byte-for-byte on all 160 cases, with all self/cross-decodes passing and no size regressions. [audit-summary.json](audit-summary.json) records per-codec counts and failing pre-test case names. This establishes the tested cross-version matrix, not complete codec correctness or the independent assembly baseline gate.

The wire-observer follow-up restores LZKN1 packed literals that the shared LZSS search had discarded. Fresh comparisons against both frozen master and the published binaries pass all 160 self/cross-decode cases, with no size regressions. Eight LZKN1 encodings now differ and become smaller; exact encoded equality is not required for this shortest-output encoder. The earlier `audit-summary.json` remains labelled with its original candidate commit.
