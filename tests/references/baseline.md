# Local stream-based baseline review

Reviewed on 2026-10-27 at production/test commit `89317c1` before the buffer refactor. No buffer API migration has started. The evidence below qualifies the declared initial corpus and configuration matrix; it does not claim all legal token-parameter combinations, optimal compression, undocumented deployment history, or real-hardware correctness.

| Local configuration | Full CTest result | Included checks |
| --- | ---: | --- |
| Windows GCC 16.2, Debug/Ninja | 134/134 | C++, assembly variants/imported originals, modules, installed static/shared consumers, stress |
| Windows Clang 23.1, Debug/Ninja | 134/134 | Same declared matrix |
| Windows MSVC 19.44, Release/VS 2022 | 135/135 | Same matrix plus published-binary comparison |
| Ubuntu/WSL GCC 15.2, Debug/Ninja, ASan/UBSan | 134/134 | Same matrix, instrumented C++ and emulator cores |

The ordinary suite has 125 entries: 43 utility, 81 codec/fixture, and one installed-package case. Assembly adds eight groups; optional stress adds one case. All existing `static_assert`s compile alongside runtime checks. Normal fixture verification requires neither network access nor Python. Imported original-derived decoder sources are an explicit maintenance step and remain outside source control.

## Encoder and oracle matrix

Every working codec has independent hand-constructed decoder vectors, actual emitted-token observations, round trips, public input/output position checks, and exact/short output-capacity checks. Module APIs additionally exercise empty/exact/partial/multiple modules, maximum representable headers, fresh history, and padding 1, 3, 16, and 32 with invalid-option rejection. Saxman has explicit framed/headerless checks; Nemesis has both automatically selected modes. Applicable external outputs must pass self/cross checks before fixture capture.

| Codec | Qualified external evidence | Assembly evidence | Module API |
| --- | --- | --- | --- |
| Comper | ClownLZSS | Shipped 68000 | C++ and assembly queue |
| ComperX | Frozen/published mdcomp comparisons; no qualified separate external encoder | Shipped 68000, both return conventions | C++ and assembly queue |
| Enigma | Qualified original KENS and shared-lineage KENSSharp cases | Imported S1-derived 68000 | None |
| Kosinski | Original KENS, KENSSharp, Accurate Kosinski, ClownLZSS | Shipped 68000/Z80 and imported S1-derived 68000 | C++ and assembly queue |
| Kosinski Plus | ClownLZSS | Shipped 68000/Z80 | C++ and assembly queue |
| LZKN1 | Frozen/published mdcomp comparisons; no qualified separate external encoder | Shipped 68000 | C++ |
| Nemesis | Qualified KENS/KENSSharp and both ClownNemesis modes | Imported S1-derived 68000 RAM decoder | None |
| Rocket | ClownLZSS | Shipped 68000 | C++ |
| Saxman | Qualified original KENS and Okumura-derived/ClownLZSS cases with target-specific verdicts | Shipped 68000 framed/headerless variants and imported S2-derived Z80 | C++ |
| SnKRLE | Kirjavascript RLE, with explicit tile-count/header conversion | Shipped 68000 | C++ |

External reference agreement is qualified by [source pins and lineage](external.md), [format verdicts](format-verdicts.md), and per-fixture metadata. KENSSharp and rewritten KENS alpha codecs share mdcomp ancestry. No independent external module-container encoder is claimed: the saved full-block/tail bodies are joined by test-owned framing, with semantic-end trimming and payload-relative padding validated separately. `artc42` is an unimplemented stub, excluded from working-codec claims.

## Recorded evidence

- [Fixture manifests](../fixtures/README.md): 96 hand-authored streams, 38 qualified external basic encodings, and 24 external full-block/tail bodies. Hash checks run through normal CTest.
- [Edge audit](edge-audit.json): all 50 encoder, 51 decoder, and 44 selected boundary obligations have validated witnesses from separate test-owned wire interpreters. Decoder-only aliases are not counted as encoder choices.
- [Direct assembly audit](assembly-audit.json): 4,340 checks across 52 images, including optimization choices and applicable imported originals. [Per-source provenance](assembly-inventory.json) records exact-revision deployment as unknown where evidence is absent.
- [Module audit](module-audit.json): 1,168 cases across eight queue images, with 3,000 real level-6 CPU interrupts and 1,510 logical DMA transfers. The [shim contract](../asm/moduled-hardware.md) distinguishes deferred logical DMA from cycle-exact VDP behavior.
- [Frozen/published binary comparisons](README.md): fresh frozen-master `72c6df405a75d322c5b3722da46c3abb864d3793` and pinned published `83ddc58527637d193b883606f8b94625574d2e0c` matrices each pass 160 self/cross cases, with zero size regressions. Eight LZKN1 encodings become smaller after the packed-literal fixes; byte equality is not required for that encoder.
- [Deterministic stress workflow](../CONTRIBUTING.md): 2,560 basic encode/decode/wire-model comparisons across four seeds and ten codecs. Failures retain their inputs, seed, encodings, and diagnostics; automatic shrinking is not implemented.
- [Compiler coverage](coverage.md): a separately verified diagnostic GCC report and optional manual CI job. Instrumentation is supplementary to semantic coverage.

The installed-package check now compiles and executes both static and shared consumers of all ten codecs without Catch2. Windows verifies the actual DLLs are loaded. It caught missing CMake exports/Boost requirements, missing MSVC DLL exports and import-library installation, and static/import filename collisions. Existing static archive and logical target names are preserved; MSVC import libraries use `-import.lib`.

Full local results are in each build's `final-baseline.log`; the Linux log is `build-linux-deps/final-baseline.log`. A separate frozen-master reproduction is in `build-reference/final-master-comparison/report.json`, with its executable/input/output hashes. These generated logs and artifacts remain in ignored build directories. Reproduce the suites with the [documented CTest configurations](../README.md), rather than treating a recorded pass as qualification of subsequent changes.

## Resolved crash and remaining qualifications

The reported Windows application-error dialog came from Catch2 3.8.1 formatting a large failing byte-vector assertion. A [standalone reporter reproduction](catch2-reporter/README.md) isolates it from Musashi. The underlying mismatch was real: interrupted ComperX modules saved only half of D4's longword RLE state. The assembly state save is repaired, mismatch diagnostics are bounded, and the assembly test main disables interactive Windows crash dialogs. No Catch2 report has been posted.

Header-limit tests also fixed truncated length fields in LZKN1, Rocket, framed Saxman, SnKRLE, and Nemesis. Counted transfers now expose buffered output failures; Enigma respects the source position and rejects incomplete words. The [low-level review](../../docs/low-level-review.md) records these and earlier fixes. Existing stream decoder error contracts remain narrower than the proposed checked-memory API; the suite does not claim that every malformed input is rejected safely by every legacy codec.

Known external defects remain in the [format verdicts](format-verdicts.md) and reference reports. Original KENS failures, KENSSharp Saxman failures, target-specific Saxman differences, and ClownLZSS full-block reference timeouts are not accepted as expected successful fixtures. The Musashi/MSVC compiler report remains queued for later, with its tested generator-only workaround intact.

The remaining baseline gate is observation of passing remote CI across the configured platforms. The first remote results and fixes are recorded below; MinGW, Linux, and macOS still need successful reruns. This session has not pushed commits. The buffer refactor remains blocked until that gate is confirmed. Checked-memory prepare/commit failures, alignment negotiation, owned/mapped resizing, stream/buffer equivalence, and allocation-free constexpr LZSS inverses depend on the future buffer implementation. Exhaustive token-parameter sweeps, automatic failure shrinking, named downstream deployment research, and real-hardware confirmation remain explicit further work. They are not silently included in the initial declared coverage claim.

## First remote CI results

The first pushed baseline, `ce8ff1c`, [passed MSVC CI](https://github.com/flamewing/mdcomp/actions/runs/40598693843). The other three workflows exposed the following blockers before their tests ran; fail-fast cancellation of sibling matrix jobs is not an additional test result.

| Workflow | Observed failure | Fix |
| --- | --- | --- |
| [MinGW Windows](https://github.com/flamewing/mdcomp/actions/runs/40598693771) | Pacman rejected the downloaded `ntldd` package checksum before checkout | Remove the unused `ntldd` dependency from this test workflow; checksum verification remains enabled. The snapshot packaging workflow still needs its separate `ntldd` dependency. |
| [Linux Clang/Ninja](https://github.com/flamewing/mdcomp/actions/runs/40598693728) | CMake's compiler probe could not link `libclang_rt.asan_static.a`, `libclang_rt.asan.a`, and `libclang_rt.asan_cxx.a` | Explicitly install `libclang-rt-19-dev` for the sanitizer matrix entry after setting up Clang 19. |
| [macOS Ninja](https://github.com/flamewing/mdcomp/actions/runs/40598693770) | Rocket's seed-fill `std::min` mixed the signed byte-count type (`long`) with `std::streamoff` (`long long`) | Compare offsets as `std::streamoff`, then convert the result bounded by the match length back to the byte-count type. |

Linux libc++ reproduced the exact macOS template-deduction error before the Rocket fix. Afterward, all codec/tool targets build with Clang 19/libc++ 18, and a public consumer round-trips all ten codecs against those shared libraries. This supplements the local Windows baseline without claiming execution on macOS. The validation uses a workspace-local linker alias for the installed libc++ unwinder runtime; no system packages were changed.

The same Clang 19 ASan/UBSan configuration as the Linux CI entry now passes all 133 baseline cases locally, including imported originals, assembly variants, module queues, and installed static/shared consumers. It uses the already installed `libclang-rt-19-dev` package. The Windows GCC configuration passes 134 cases including its optional stress sweep. Local logs are `build-linux-deps/clang-ci-{config,build,test}.log`, `build-linux-deps/libcxx-ci-{config,build}.log`, `build-linux-deps/libcxx-consumer.log`, and `build-assembly/ci-fix-test.log`. The fixes still require pushed remote reruns; local qualification does not turn the earlier failed workflows into successful runs.
