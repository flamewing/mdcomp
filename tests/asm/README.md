# Assembly decoder tests

The opt-in suite builds the pinned ASL host tools with only `M68K;Z80` backends, assembles decoder images, and executes them using Musashi's MC68000 core and superzazu/z80. It runs the actual instructions rather than translating assembly into C++. Dependencies are pinned submodules; normal builds do not download tools or original sources. Nothing from this suite is installed or exported.

```sh
git submodule update --init --recursive
python tests/support/import_sega_decoders.py --destination build-reference/sega-decoders
cmake -S . -B build-tests -DBUILD_TESTING=ON -DMDCOMP_ASSEMBLY_TESTS=ON -DMDCOMP_SEGA_DECODER_DIR="$PWD/build-reference/sega-decoders"
cmake --build build-tests --parallel
ctest --test-dir build-tests -L asm --output-on-failure
```

Supply Catch2 v3 as in [the test workflow](../README.md). With Visual Studio, add `--config Release` to the build and `-C Release` to CTest. Omitting `MDCOMP_SEGA_DECODER_DIR` tests just the ten shipped direct routines. Turning `MDCOMP_ASSEMBLY_TESTS` off restores the ordinary offline C++ suite. Native host builds are supported; cross-compiling the test tools is not yet supported.

The original-source importer is an explicit maintenance/setup action. It checks full-file and extracted-block hashes before writing anything. For offline setup, append `--offline-dir /path/to/cache`, containing `kosinski.asm`, `enigma.asm`, `nemesis.asm`, and `s2.sounddriver.asm`. CMake also checks imported decoder hashes. [The source manifest](../references/sega-decoders.json) records URLs, revisions, transformations, and attribution. These are original-derived routines from pinned game disassemblies, not pristine recovered Sega build sources. Their license grants are not specified in the imported files, so the sources remain out of tree; no ROM assets are used.

## Execution and qualification

Each ASL wrapper exports an entry, payload address, and completion address in a small binary header. Sources and `_inc` paths are staged below the build directory without rewriting their instructions. Distinct wrapper filenames avoid collisions on case-insensitive filesystems. Original Z80 Saxman additionally exports the two immediate words it modifies; those four bytes are the only writable code locations. `OptimiseDriver=0` selects the original driver path.

The MC68000 machine sets A0 to the payload, A1 to output, and A4 to output for the original Nemesis RAM entry; D0 supplies Enigma's starting art tile. The Z80 machine sets HL to payload and DE to output. Every run starts from deterministic CPU state, with a fresh MC68000 reset, initialized return address, and bounded stack. Completion requires the return sentinel and a balanced stack. Code, compressed data, exact-sized poisoned output, stack, and Nemesis scratch are mapped separately. Bounds, original-68000 word alignment, unexpected ports, writes to immutable data/code, and reads from unwritten output/stack are checked.

Default limits are two million instructions and fifty million core cycles, with a separate CTest timeout. Most decoders get no readable bytes beyond the attached compressed stream; Enigma/Nemesis permit two read-ahead bytes and SnKRLE permits the one byte documented for MOVEP. Read high-water marks are recorded. A guard failure stops the harness; it does not pretend that the original decoder validates malformed input or that real hardware would raise the same error.

CTest registers five groups: `asm::machine`, `asm::bootstrap`, `asm::external`, `asm::encode`, and `asm::negative`. Machine calibration verifies both sentinels, read/write bounds, budgets, alignment, and recovery after a fault. Fixture dependencies prevent encoder interoperability checks from running when calibration or known-answer bootstrap fails. Running `ctest --test-dir build-tests -R 'asm::encode' --output-on-failure` automatically adds the required setup tests.

The current default-configuration audit has **446 passing runs across 14 images**: 133 known-answer runs (131 hand-constructed plus the published Enigma example at a nonzero starting tile on both implementations), 65 saved external encodings, 233 C++ encoder runs, and 15 containment/target-limitation checks. [assembly-audit.json](../references/assembly-audit.json) records per-image hashes, source/wrapper hashes, addresses, provenance, observed read-ahead, and counts.

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

## Defects exposed and fixed

- The ComperX wrapper and internal body both defined `ComperXDec`, preventing assembly; the wrapper now uses the included body's public label.
- Saxman's pre-read counter rejected equality, dropping the last literal or dictionary token. It now allows the final complete read and stops on underflow.
- Both Z80 Kosinski variants executed `LDIR` with BC=0 for a two-byte short match, which means 65536 copies. They now skip that bulk copy and perform the required two `LDI`s. The added code required one formerly short Kosinski branch to become `JP`.
- Rocket used an uninitialized register for its position bias, failed to rebase matches around the current destination, and filled more seeded spaces than short matches requested. Those paths are corrected and cover seeded-only, overlap, and seed-to-output transitions. Its compressed length is also treated as unsigned; a deterministic 32768-byte random input exercises the high bit.

MSVC 19.44 x64 `/O2` miscompiles the unsigned-short bit-pattern accumulation in the pinned Musashi build-time opcode generator, causing calibration to crash. The [root-cause investigation](../references/musashi-msvc/README.md) includes a standalone reproducer, instruction-level evidence, and a one-line source workaround whose full generated output matches GCC. Only the generator is compiled with `/Od` locally; the emulator core remains optimized in Release. The pinned upstream source is not patched.

## Reports and remaining work

The five TSV files below `build-tests/tests/asm` record each decoder/case, success flags, instruction/cycle counts, input high-water mark, compressed size, and read-ahead allowance. `failures/asm/<decoder>/<fixture>` contains compressed input, expected/actual output, PC/register dumps, and the recent memory accesses. CI preserves synthetic failure data and reports, without publishing imported original source or decoder binaries.

After a reviewed complete run, update the committed summary explicitly:

```sh
python tests/support/record_assembly_audit.py --test-dir build-tests/tests/asm --output tests/references/assembly-audit.json
```

This checkpoint is verified locally with GCC 16.2, Clang 23.1, and MSVC 19.44 on Windows. CI is configured to exercise the suite on Linux, macOS, MinGW, and MSVC; those remote jobs were not run in this local session. The ordinary suite remains 111 CTest cases; assembly adds five groups, for 116 entries with original sources enabled and 117 when the optional master-binary comparison is also enabled.

Remaining work includes alternative LUT/unroll/alignment configurations, complete semantic encoder/decoder edge inventories, broader malformed-input cases, and real-hardware confirmation. The common moduled queue/DMA/interrupt framework still needs its hardware shim; this CPU-only checkpoint does not test those wrappers. The refactor gate remains blocked until the remaining testing-plan obligations are complete.
