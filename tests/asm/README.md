# Assembly decoder tests

The opt-in suite builds the pinned ASL host tools with only `M68K;Z80` backends, assembles decoder images, and executes them using Musashi's MC68000 core and superzazu/z80. It runs the actual instructions rather than translating assembly into C++. Dependencies are pinned submodules; normal builds do not download tools or original sources. Nothing from this suite is installed or exported.

```sh
git submodule update --init --recursive
python tests/support/import_sega_decoders.py --destination build-reference/sega-decoders
cmake -S . -B build-tests -DBUILD_TESTING=ON -DMDCOMP_ASSEMBLY_TESTS=ON -DMDCOMP_SEGA_DECODER_DIR="$PWD/build-reference/sega-decoders"
cmake --build build-tests --parallel
ctest --test-dir build-tests -L asm --output-on-failure
```

Supply Catch2 v3 as in [the test workflow](../README.md). With Visual Studio, add `--config Release` to the build and `-C Release` to CTest. Omitting `MDCOMP_SEGA_DECODER_DIR` excludes the four original-derived routines. Turning `MDCOMP_ASSEMBLY_TESTS` off restores the ordinary offline C++ suite. Native host builds are supported; cross-compiling the test tools is not yet supported.

The original-source importer is an explicit maintenance/setup action. It checks full-file and extracted-block hashes before writing anything. For offline setup, append `--offline-dir /path/to/cache`, containing `kosinski.asm`, `enigma.asm`, `nemesis.asm`, and `s2.sounddriver.asm`. CMake also checks imported decoder hashes. [The source manifest](../references/sega-decoders.json) records URLs, revisions, transformations, and attribution. These are original-derived routines from pinned game disassemblies, not pristine recovered Sega build sources. Their license grants are not specified in the imported files, so the sources remain out of tree; no ROM assets are used.

## Execution and qualification

Each ASL wrapper exports an entry, payload address, and completion address in a small binary header. Sources and `_inc` paths are staged below the build directory without rewriting their instructions. Distinct wrapper filenames avoid collisions on case-insensitive filesystems. Original Z80 Saxman additionally exports the two immediate words it modifies; those four bytes are the only writable code locations. `OptimiseDriver=0` selects the original driver path.

The MC68000 machine sets A0 to the payload, A1 to output, and A4 to output for the original Nemesis RAM entry; D0 supplies Enigma's starting art tile. The four `saxman-body-lut{0,1}-odd{0,1}` images enter `SaxDec2` with D6 holding the unsigned 16-bit body length, including zero. Known-answer and external sized fixtures have only their two-byte header removed, and current-encoder fixtures use the headerless API. Tests exercise both source alignments and LUT choices; a zero body leaves trailing bytes unread. The C++ stream encoder aligns its output file to an even byte count; use the framed header to distinguish the body from that padding. The runner defaults D6 to the input length; `--compressed-size N` bounds it explicitly when the file includes trailing data. This assembly ABI can express a zero-length body independently of the C++ decoder API, whose zero size argument selects framed decoding.

The Z80 machine sets HL to payload and DE to output. Every run starts from deterministic CPU state, with a fresh MC68000 reset, initialized return address, and bounded stack. Completion requires the return sentinel and a balanced stack. Code, compressed data, exact-sized poisoned output, stack, and Nemesis scratch are mapped separately. Bounds, original-68000 word alignment, unexpected ports, writes to immutable data/code, and reads from unwritten output/stack are checked.

Default limits are two million instructions and fifty million core cycles, with a separate CTest timeout. Most decoders get no readable bytes beyond the attached compressed stream; Enigma/Nemesis permit two read-ahead bytes and SnKRLE permits the one byte documented for MOVEP. Read high-water marks are recorded. A guard failure stops the harness; it does not pretend that the original decoder validates malformed input or that real hardware would raise the same error.

The direct decoder checks use five groups: `asm::machine`, `asm::bootstrap`, `asm::external`, `asm::encode`, and `asm::negative`. Machine calibration verifies both sentinels, read/write bounds, budgets, alignment, and recovery after a fault. Fixture dependencies prevent encoder interoperability checks from running when calibration or known-answer bootstrap fails. Running `ctest --test-dir build-tests -R 'asm::encode' --output-on-failure` automatically adds the required setup tests.

The initial default-configuration checkpoint had **446 passing case checks across 14 images**: 133 known-answer checks (131 hand-constructed plus the published Enigma example at a nonzero starting tile on both implementations), 65 saved external encodings, 233 C++ encoder checks, and 15 containment/target-limitation checks. The expanded [assembly-audit.json](../references/assembly-audit.json) now records **4,340 passing decoder/case checks across 52 images**: 582 original known-answer checks, 2,348 optimization-tail known-answer checks, 471 external encodings, 886 C++ encodings, and 53 containment checks. These counts exclude calibration, CLI checks, and additional stack-poison replays. The original fourteen images remain byte-for-byte unchanged. Per-image hashes, source/wrapper hashes, configuration, source alignment, scratch requirements, provenance, observed read-ahead, and counts are retained.

| Image | CPU | Known answers | External encodings | C++ encodings | Qualification/configuration |
| --- | --- | ---: | ---: | ---: | --- |
| Comper | 68000 | 7 | 4 | 17 | Shipped hand-crafted/reference-derived routine; loop unroll 3 |
| ComperX | 68000 | 9 | 0 | 17 | Shipped vladikcomper routine; RTS completion |
| Kosinski | 68000 | 14 | 8 | 17 | Shipped optimized routine; LUT, unroll 3, extreme unrolling |
| Kosinski Plus | 68000 | 13 | 4 | 17 | Shipped optimized routine; unroll 3 |
| Enigma | 68000 | 14 | 3 | 17 | Edited original-derived routine; zero and nonzero starting tiles |
| Saxman | 68000 | 6 | 4 | 17 | Shipped rebasing routine; LUT and aligned-source configuration |
| Rocket | 68000 | 5 | 2 | 18 | Shipped routine; LUT/unroll 3; includes unsigned payload length above 32767 |
| SnKRLE | 68000 | 2 | 6 | 16 | Shipped routine; nonzero even output and aligned source/output |
| Kosinski | Z80 | 14 | 8 | 17 | Shipped routine; early descriptor reload |
| Kosinski Plus | Z80 | 13 | 4 | 17 | Shipped routine |
| Sega-derived Kosinski | 68000 | 14 | 8 | 17 | Sonic 1 disassembly; source unchanged |
| Sega-derived Enigma | 68000 | 14 | 3 | 17 | Sonic 1 disassembly; source unchanged |
| Sega-derived Nemesis | 68000 | 4 | 7 | 16 | Sonic 1 RAM entry; code-table scratch mapped, VDP path not executed |
| Sega-derived Saxman | Z80 | 4 | 4 | 13 | Sonic 2 original path; self-modification constrained to exported words |

Shared mdcomp authorship/design lineage is retained for the shipped routines. Reference-generated fixtures retain their KENS/KENSSharp/Clownacy/RLE lineage. Passing both an optimized decoder and an original-derived one supplies triangulation; neither is described as an infallible oracle. Deployment has not been independently surveyed, and these results make no new field-use or real-hardware claim.

Some C++ cases intentionally do not apply to every assembly target. SnKRLE requires nonzero even output; zero-tile Nemesis is a C++ empty-input contract. Headerless Saxman fixtures require a different calling entry and are not fed to the sized entry. Original Z80 Saxman is restricted to its 0x800-byte music buffer. Its initial self-reference aliases do not guarantee the zero-filled history of the rebasing/68k interpretation: an explicit negative test confirms the original instructions read unwritten output. These cases are qualified rather than silently treated as interchangeable format variants.

### Optimization configuration matrix

`MDCOMP_ASSEMBLY_VARIANTS` defaults to `ON` when assembly tests are enabled. It adds 34 images to the ten shipped/default entries, four SaxDec2 body-entry images, and four optional original-derived entries. Set `-DMDCOMP_ASSEMBLY_VARIANTS=OFF` to retain defaults and the four SaxDec2 body-entry images; the new hand-authored tail fixtures still run against applicable defaults. The checked unroll levels are 0, 1, 2, and 3, corresponding to blocks of 1, 2, 4, and 8 copies. Larger levels are not qualified by this checkpoint.

| Routine | Matrix including the default | Images |
| --- | --- | ---: |
| Comper | Four unroll levels | 4 |
| ComperX | RTS and A5 register-return paths | 2 |
| Kosinski 68000 | Four unroll levels × LUT off/on × extreme unrolling off/on | 16 |
| Kosinski Plus 68000 | Four unroll levels | 4 |
| Rocket | Four unroll levels × LUT off/on | 8 |
| Saxman 68000 | LUT off/on × aligned/byte-header paths with even input, plus byte-header paths with odd input | 6 |
| Remaining defaults and original-derived references | Existing configuration each | 8 |

The source option assignments are now guarded by `defined()` checks, allowing wrappers to supply settings without rewriting staged decoder instructions. Existing defaults and their binary images are unchanged. `variants.cmake` declares the matrix; generated wrappers export the actual entry/payload, and the registry records the chosen configuration. `mdcomp-asm-run --list` displays it, and JSON timing includes a `configuration` field. For example, `mdcomp-asm-run kosinski-u0-lut0-x0 input.kos output.bin --json` runs the scalar/no-LUT/non-extreme configuration. ComperX's register-return wrapper initializes A5 to an RTS continuation, exercising both termination slots while preserving the harness stack convention.

The 307 additional hand-authored streams exercise every small tail modulo 2, 4, and 8, larger loop/counter boundaries, all short/medium Kosinski match lengths, extended counts including 256/264, both ComperX copy forms and termination slots, and all Rocket/Saxman match lengths in seeded/overlap forms. A shared test-only descriptor builder constructs bytes without production endian/bitstream/LZSS helpers; expected output follows explicit literals and repetitions. C++ decoder checks corroborate these expectations rather than supply them. The optimization case belongs to bootstrap, so a failing configuration prevents encoder interoperability checks from running. Timing for `comperx-jump` includes the wrapper's LEA/BRA setup and RTS continuation; account for that overhead when comparing it with direct RTS completion.

Saxman's byte-header path intentionally writes one stack byte and reads the containing word before replacing the discarded byte. This is valid 68000 code, but the initial harness's unwritten-stack check rejected that unused byte. Only these four configurations declare two readable scratch bytes immediately below the initial SP, initialized by the host outside timed execution. Both `0xa5` and `0x5a` poison patterns must yield the same bytes and cycle counts. A calibration test confirms that omitting the declaration still fails the stack guard, while declaring it permits both poisons. Other unwritten-stack/output reads remain guarded. This is a harness-model qualification, not a decoder defect.

## Defects exposed and fixed

- The ComperX wrapper and internal body both defined `ComperXDec`, preventing assembly; the wrapper now uses the included body's public label.
- Saxman's pre-read counter rejected equality, dropping the last literal or dictionary token. It now allows the final complete read and stops on underflow.
- Both Z80 Kosinski variants executed `LDIR` with BC=0 for a two-byte short match, which means 65536 copies. They now skip that bulk copy and perform the required two `LDI`s. The added code required one formerly short Kosinski branch to become `JP`.
- Rocket used an uninitialized register for its position bias, failed to rebase matches around the current destination, and filled more seeded spaces than short matches requested. Those paths are corrected and cover seeded-only, overlap, and seed-to-output transitions. Its compressed length is also treated as unsigned; a deterministic 32768-byte random input exercises the high bit.

MSVC 19.44 x64 `/O2` miscompiles the unsigned-short bit-pattern accumulation in the pinned Musashi build-time opcode generator, causing calibration to crash. The [root-cause investigation](../references/musashi-msvc/README.md) includes a standalone reproducer, instruction-level evidence, and a one-line source workaround whose full generated output matches GCC. Only the generator is compiled with `/Od` locally; the emulator core remains optimized in Release. The pinned upstream source is not patched.

## Reports and remaining work

### Running files and measuring decoder timing

The `mdcomp-asm-run` executable uses the same registry, ASL images, memory guards, and CPU cores as the fixtures. Build it with `cmake --build build-tests --target mdcomp-asm-run` (add `--config Release` for Visual Studio). The executable is under `build-tests/tests/asm`, or its `Release` subdirectory with Visual Studio. It does not require Catch2 at runtime. List the available compiled-in decoders with `--list`:

```sh
build-tests/tests/asm/mdcomp-asm-run --list
build-tests/tests/asm/mdcomp-asm-run kosinski art.kos art.bin
build-tests/tests/asm/mdcomp-asm-run kosinski-z80 art.kos art-z80.bin --output-size 4096
build-tests/tests/asm/mdcomp-asm-run rocket art.roc art.bin --json
build-tests/tests/asm/mdcomp-asm-run enigma map.eni map.bin --art-base 0x1000
```

Normal output includes decoded size, instruction count, and timing: `112(19/4)` for the 68000 calibration program or `99 T-states` for Z80. JSON output contains numeric `cycles`, `instructions`, `output_bytes`, and `input_high_water` fields, a formatted `timing` string, and a `timing_model` identifier; the 68000 also includes `bus_reads` and `bus_writes`. Counts cover the decoder entry through its final RET/RTS. Host memory initialization, reset-vector reads, reset clocks, and installation of the synthetic return address are excluded. They are emulated CPU counts, not elapsed host time. The models assume no memory wait states, bus contention, interrupts, or Mega Drive DMA effects.

**68000 bus qualification:** `m` is Musashi's instruction clock count. `r` and `w` count successful logical memory transfers exposed by its callbacks, including opcode/extension fetches, data, and emulated stack accesses. Byte/word callbacks count once; longword callbacks count twice for the 16-bit bus. Musashi is not a cycle-exact bus emulator: some speculative prefetches, discarded extension fetches, and dummy reads are absent. These counts therefore do not reproduce the Motorola manual's physical bus totals for every instruction. For example, the copy calibration gives modeled `112(19/4)`, whereas summing the manual's instruction timings gives `112(22/4)`; its expired DBRA and RTS account for the missing reads. JSON labels this model `musashi-logical-16-bit-transfers`, and human output explicitly identifies the limitation. The Z80 value is the core's T-state counter (`z80-core-tstates`). A complete hardware-accurate bus model remains future work.

The instruction clock and bus-count convention is documented in [MC68000 User's Manual, section 8](https://www.nxp.com/docs/en/reference-manual/MC68000UM.pdf). The calibration fixes expected totals independently: MOVEQ 4, four MOVE.B copies 48, three taken DBRAs 30, one expired DBRA 14, and RTS 16 give 112 clocks; LD BC 10, three repeated LDIR iterations 63, its final iteration 16, and RET 10 give 99 T-states. A separate longword copy checks that each longword transfer counts as two word-bus accesses. Setup exclusion and repeatability are also tested.

By default, the runner returns the contiguous output actually written, with a cap of 512 KiB on the 68000 and 24 KiB on Z80; original Sonic 2 Saxman is capped at its 2 KiB music buffer. `--output-limit N` lowers the cap; `--output-size N` additionally requires exactly N bytes. Decoding failures leave an existing output file unchanged. Input/output and decoder-image/output aliases are rejected. Bounds and cycle/instruction budgets remain active; `--instruction-limit N` and `--cycle-limit N` can raise them for larger inputs. The compressed input limit is 128 KiB on the 68000 or 16 KiB on Z80, less the decoder's readable-padding allowance. Format-specific empty/alignment/framing preconditions still apply.

For optimization experiments, `--image FILE` replaces the selected decoder's image with a newly assembled wrapper using the same header and calling convention. The selected registry entry still supplies CPU, read-ahead, and self-modification rules. `--image-dir DIR` selects a different directory of built images; it also lets the executable run after moving the images away from the original build directory. Existing ASL listings and wrapper sources under `tests/asm/images` provide starting points. Re-run with the same input, art base, output constraints, and core version when comparing routines, and verify decoded bytes before interpreting timing changes.

The six TSV files below `build-tests/tests/asm` (`bootstrap`, `optimization`, `external`, `encode`, `rocket-large`, and `negative`) record each decoder/case, success flags, instruction/cycle counts, input high-water mark, compressed size, and read-ahead allowance. They append three columns: modeled bus reads, modeled bus writes, and formatted timing. The complete column order is `decoder`, `case`, `success`, `byte_match`, `instructions`, `cycles`, `input_high_water`, `compressed_size`, `readable_padding`, `bus_reads`, `bus_writes`, `timing`; files remain headerless. Z80 bus columns are zero because its reported performance metric is T-states. Timing is also included in Catch2 assertion context; run `mdcomp-asm-tests "[bootstrap]" --success` to print passing fixture context. The audit recorder accepts both the older nine-column and new twelve-column reports. `failures/asm/<decoder>/<fixture>` contains compressed input, expected/actual output, PC/register dumps, and the recent memory accesses. CI preserves synthetic failure data and reports, without publishing imported original source or decoder binaries.

After a reviewed complete run, update the committed summary explicitly:

```sh
python tests/support/record_assembly_audit.py --test-dir build-tests/tests/asm --output tests/references/assembly-audit.json
```

This checkpoint is verified locally with GCC 16.2, Clang 23.1, and MSVC 19.44 on Windows. CI is configured to exercise the suite on Linux, macOS, MinGW, and MSVC; those remote jobs were not run in this local session. The ordinary suite has 120 CTest cases; assembly adds eight groups including CLI and moduled checks, for 128 entries with original sources enabled and 129 when the optional binary comparison is also enabled.

The [moduled hardware shim](moduled-hardware.md) now executes all four shipped queue wrappers under both DMA address conventions, including real CPU interrupt/save/resume flows. Its separate audit records 1,168 cases across eight images. Two additional CTest groups, `asm::module-bootstrap` and `asm::module-encode`, bring the suite to 128 entries, or 129 with the optional frozen-master comparison. Module fixtures are logical DMA/interrupt integration tests rather than VDP timing emulation; the direct file runner remains for direct decoder images.

The [semantic edge manifest](../fixtures/edges.json) now checks independently observed wire forms and selected boundaries. Remaining work includes exhaustive parameter matrices, broader malformed-input cases, configurations outside the declared matrix, deployment-history research, and real-hardware confirmation. The refactor gate remains blocked until the remaining testing-plan obligations are complete.

Qualified saved Comper, Kosinski, and Kosinski Plus external encodings also bootstrap the module queues under both DMA source conventions. Only a test-owned outer size header is added; this checks the queue with a short final module and does not claim that the external tools implement this module container. The module audit now records 1,168 cases, including 32 such external-body checks, 2,392 interrupts, and 1,438 transfers.

## Per-source qualification

[assembly-inventory.json](../references/assembly-inventory.json) records each of the ten shipped direct source files and four imported originals separately. It preserves exact header credits where available, distinguishes format-design credits from routine/port authorship, and labels maintainer evidence where the source header does not establish origin. `record_assembly_audit.py` requires a matching inventory record for every staged decoder source and carries its attribution, provenance, evidence, license qualification, deployment status, and real-hardware status into each image record. Configuration and emulator results stay separate.

No exact shipped revision has a newly established named downstream deployment in this audit. The original-derived routines are located in the pinned Sonic 1/Sonic 2 disassemblies; that alone does not establish execution of every exported entry or a byte-identical recovered Sega build. Named projects/versions and real-hardware confirmation remain open evidence requests. Unknown deployment is not a claim of no use.

The assembly test main prints configure-time dependency commit IDs, fault artifacts retain them, and the direct runner includes them in both normal output and JSON `dependencies`. Source archives without a Git checkout report `unknown`; parent-repository commits are not substituted. Reconfigure after changing dependency checkouts. These identities do not certify that a working tree is unmodified; use the pinned submodule status and recorded source/image hashes alongside them.

The paired external full-block corpus adds canonical 8,256-byte archives to the module bootstrap: two full 4 KiB modules plus a 64-byte tail, both DMA source ABIs, and repeated real interrupts. Direct decoder checks also consume all applicable new reference bodies. With the ComperX terminal-copy regressions, the current audits record 4,340 direct checks and 1,168 module checks, with 3,000 interrupts and 1,510 transfers. The full suite contains 133 baseline CTest cases; optional stress and binary comparison each add one. The [local baseline review](../references/baseline.md) records the final compiler matrix and remaining remote-CI gate.

ASL was updated to `4b26399280c3a9b57676288b0984f667da412e4a` on 2026-10-28. A fresh Windows GCC Debug build passes all 134 current CTest cases, including the assembly groups and installed-package check. All 64 assembled fixture images are byte-identical to those built with the previous ASL pin, `41c70d8895daf1a16ca3c3a591477f064a7030bb`. Configure-time dependency diagnostics record the new revision; historical audit checkpoints retain their original provenance.
