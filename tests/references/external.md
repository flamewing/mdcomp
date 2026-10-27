# External codec comparisons

[Format-based verdicts](format-verdicts.md) extend this audit with Clownacy's Accurate Kosinski, Okumura-derived Saxman, ClownLZSS, and ClownNemesis implementations. That follow-up identifies a real mdcomp Saxman boundary defect and distinguishes reference bugs from semantic variants. The table below preserves the earlier candidate checkpoint; `*-after-saxman` records in `external-audit.json` contain the repaired results. The current offline corpus contains 38 qualified external encodings.

[external.json](external.json) pins the source archives, commits, hashes, licenses, build adjustments, and implementation lineage. [external-audit.json](external-audit.json) records binary hashes, per-codec results, and every incomplete or failing case. Reference source and binaries remain in ignored build directories. Normal C++ tests use the captured synthetic-input encodings in [external.json](../fixtures/external.json); they do not download or execute third-party tools.

Original KENS matters separately from KENSSharp: KENS predates the flamewing implementation and only loosely influenced it. KENSSharp derives from an earlier version of that implementation and can inherit bugs or contain independent fixes. The SourceForge alpha is also partly shared lineage: its `ChangeLog.htm` explicitly says Kosinski, Enigma, and Nemesis were replaced with flamewing routines; it retains the older Saxman code. None is assumed correct merely because it is a reference.

The SourceForge archive named `kens-1.4.22.tar.xz` under `kens-cmake/1.5 alpha` extracts to `kens-1.5a1`, declares CMake project `kens_1.4.22`, and describes itself as unstable. These labels refer to the tested archive, not two independently verified releases. The supplied `kens-vc++/1.4` listing contains RC1/RC3 Win32 files. The primary RC3 distribution and its matching source were retrieved there. The DLLs preserved in Clownacy's frontend repository were rebuilt in 2015 and differ from the primary 2004 distribution; their supplementary results are kept separately and their encodings are not used in committed fixtures.

Every case starts with independently generated raw bytes. Both tools encode them; both decoders read each encoding. Success requires exact bytes and length in all four directions. Encoded-byte equality and relative compressed size are not correctness requirements across these different implementations. The common corpus is four patterns at 32, 64, 256, and 512 bytes. RLE adds four cases around 257/258-byte runs and final runs/pairs. Every invocation has a timeout, an output limit, a separate log, and Windows loader/crash-dialog suppression. The original DLLs use fixed temporary filenames, so do not run original-KENS comparisons concurrently.

The completed Windows/GCC candidate audit is:

| Reference | Cases | All four directions pass | mdcomp output accepted by reference decoder |
| --- | ---: | ---: | ---: |
| Primary KENS 1.4 RC3 | 64 | 36 | 48 |
| KENS 1.4.22 / 1.5 alpha archive | 64 | 40 | 48 |
| KENSSharp pinned master | 96 | 80 | 96 |
| kirjavascript RLE | 20 | 20 | 20 |

All candidate self-decodes pass. The 16 rejected mdcomp outputs in each native KENS row are Saxman cases; those reference decoders also fail their own encodings. Primary RC3 Enigma fails its four zero-input self-checks; primary RC3 Nemesis fails eight ascending/random cases. The alpha Enigma fails eight ascending/random self-checks. These counts describe the tested binaries/builds, including failures; they do not establish universal format incompatibility. The mirrored DLLs have different failures, which is why primary provenance and executable hashes are retained.

Source inspection explains two important disagreements. KENSSharp's sized Saxman encoder computes `outputInitialPosition - output.Position - 2`, producing an incorrect length field in twelve cases; four all-zero cases instead throw during zero-match search before producing a file. Its decoder accepts all 16 mdcomp Saxman outputs. Original RC3 Saxman compares the file position against `Location + Size` after reading the header and uses a strict cutoff after reading token bytes; this is inconsistent with the body length written by its encoder. The later format audit also established that all sixteen RC3 Saxman encodings are correct; seven old mdcomp cross-decoding failures were caused by mdcomp's added extraction padding. No fixes to third-party codec logic were applied to make an audit pass. Only reference-generated encodings that passed all four checks were captured, and their provenance remains distinct from hand-constructed fixtures.

Kirjavascript's `comp.c` uses a big-endian tile count, whereas mdcomp SnKRLE uses a big-endian byte count. Cross-decodes multiply or divide that header by 32 and leave the token body untouched. Test inputs meet the original tool's tile alignment and size restrictions. The committed RLE fixtures retain both original and converted hashes and record the transformation. No license is specified in that repository, so its source and binaries are not vendored. This checkpoint executes its C encoder/decoder; execution of `decomp.asm` still belongs to the assembly harness work.

## Reproducing the audit

Download the pinned files listed in `external.json`, verify their SHA-256 hashes, and extract them below `build-reference`. SourceForge may first serve a download HTML page; follow its direct-download link and verify the resulting archive, rather than treating that page as the source file. Do not silently substitute a newer release or the GitHub DLL mirror.

Compile the original DLL wrapper with the .NET Framework C# compiler (Windows paths below):

```powershell
& C:/Windows/Microsoft.NET/Framework64/v4.0.30319/csc.exe /nologo /platform:x86 /out:build-reference\kens-win-distribution\kens.exe tests\support\original_kens.cs
python tests/support/compare_external.py --reference-kind kens --reference-dir build-reference/kens-win-distribution --candidate-dir build-tests --output-dir build-reference/kens-rc3-comparison
```

Build the alpha with GCC in C++98 mode. Apply [kens15-portability.patch](kens15-portability.patch) to its extracted source first: it changes unavailable `sys/io.h` includes to `io.h` and makes two comparators callable through const references. It does not change token processing. The patched archive is a qualified source-build reference, not the untouched published executable.

```sh
cmake -S build-reference/kens15-source/kens-1.5a1 -B build-reference/kens15-build -G Ninja -DCMAKE_CXX_STANDARD=98 -DCMAKE_POLICY_VERSION_MINIMUM=3.5
cmake --build build-reference/kens15-build --parallel
python tests/support/compare_external.py --reference-kind kens15 --reference-dir build-reference/kens15-build/src/tools --reference-runtime-dir build-reference/kens15-build/src/kens --candidate-dir build-tests --output-dir build-reference/kens15-comparison
```

Build KENSSharp's `KensSharp/KensSharp.csproj` with VS 2022 MSBuild using `/t:Rebuild /p:Configuration=Release /p:Platform=AnyCPU /p:TargetFrameworkVersion=v4.8 /p:TargetFrameworkProfile=`. Set the generated `KensSharp.exe.config` startup to `<supportedRuntime version="v4.0" sku=".NETFramework,Version=v4.8"/>`; the original config selects runtime v2, which cannot load the retargeted executable. Codec source remains unchanged. Pass its `KensSharp/bin/Release` directory to `--reference-dir` and use `--reference-kind kenssharp`.

Build unmodified RLE `comp.c` with `gcc -std=c11 -O2 -o build-reference/snkrle/rle.exe build-reference/snkrle/comp.c`, then use `--reference-kind snkrle --reference-dir build-reference/snkrle`. On non-Windows hosts, omit the executable suffix. Candidate MSVC tools can need `--candidate-runtime-dir build-msvc/vcpkg_installed/x64-windows/bin`. Comparisons with observed reference failures deliberately return nonzero; inspect the report instead of suppressing them or blessing their output.

After reviewing all disagreements, capture the small qualified corpus and verify its committed representation:

```sh
python tests/support/capture_external_fixtures.py --comparison-root build-reference --output-dir tests/fixtures
python tests/support/capture_external_fixtures.py --output-dir tests/fixtures --check
ctest --test-dir build-tests -L external --output-on-failure
```

Capture expects the `kens-rc3-comparison`, `kenssharp-comparison`, `snkrle-comparison`, `accurate-kosinski-comparison`, `original-saxman-comparison`, `clownlzss-comparison`, `clownnemesis-comparison`, and `clownnemesis-accurate-comparison` directories. Updating references requires reviewing pins, executable hashes, both comparison directions, and the fixture diff. Saved fixtures are reference-generated evidence for the selected cases; they do not replace full token-edge coverage or assembly execution.

## Module-container qualification

The offline suite retains the 38 saved basic encodings and also checks applicable bodies inside test-constructed short-final-module containers. The only added bytes are the outer big-endian uncompressed-size header; original encoded bytes and lineage remain unchanged. C++ module decoders exercise padding settings 1, 3, 16, and 32 and an existing output prefix. Applicable Comper/Kosinski/Plus bodies also execute through both assembly DMA address conventions. These are container interoperability checks, not encodings produced by an independent module encoder. A separate wire interpreter validates all eight current module encoders, including multi-module boundaries, payload-relative zero padding, and per-module history reset. Reference-generated full multi-module corpora and exhaustive reference encoder options remain open.
