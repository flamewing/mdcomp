# Format-based verdicts

Follow-up: the [assembly execution audit](../asm/README.md) now corroborates applicable fixtures with ten shipped MC68000/Z80 routines and four pinned original-derived Sega routines. It confirms the original Z80 Saxman initial full-window alias reads unwritten output, while the rebasing MC68000 target supplies zeros. Its 446 recorded executions cover default direct configurations; the pending assembly statements below describe the earlier source/interpreter checkpoint.

The comparison matrix finds disagreements; it does not decide which implementation is correct. This audit interprets the disputed bytes using the format rules, checks independently known output, and distinguishes valid alternative encodings from incorrect output and process failures. [format-verdicts.json](format-verdicts.json) records the results; [external.json](external.json) pins source/dependency versions, licenses, and build details. No third-party codec logic was patched to obtain passing results.

`tests/support/audit_formats.py` contains bounded, test-only interpreters for Kosinski, Saxman, Enigma, and Nemesis. They use no production codec, bitstream, endian, or LZSS helpers. They reproduce two published examples and all 38 applicable hand-constructed fixtures, including explicit lengths and bytes. Zero-tile Nemesis is checked as the existing C++ empty-input contract; it is not evidence that the original assembly supports that framing. These interpreters are additional evidence, not formally verified specifications or executed Sega assembly.

The rules come from the [Saxman description](https://info.sonicretro.org/Saxman_compression), the repository's [Enigma description](../../docs/Enigma%20compression%20-%20Sega%20Retro.pdf) and [Nemesis description](../../docs/Nemesis%20compression%20-%20Sega%20Retro.pdf), and the shipped decoder grammars. Kosinski's early descriptor reload, match forms, and control markers were also checked against the pinned [Accurate Kosinski decoder](https://github.com/Clownacy/accurate-kosinski/blob/45abe26a5882ef27a18f3f0052b6ef063bbbc3a8/lib/kosinski-decompress.c). The online Kosinski description could not be retrieved during this audit; its grammar qualification therefore rests on those decoder sources and explicit fixture bytes.

## Decisions

| Disagreement | Verdict for the documented target | Evidence and resulting action |
| --- | --- | --- |
| mdcomp vs KENS Saxman streams with an unused final descriptor | mdcomp decoder was wrong | Extraction added an alignment zero beyond the declared body. The decoder interpreted it as token data and emitted three extra bytes. Fixed by reading exactly the body length, preserving the next stream, and rejecting truncated framing/tokens. |
| Primary KENS/alpha Saxman decoding ordinary valid streams | Reference decoders are wrong | Their end calculation includes the header in the body budget and rejects the final byte with `>=`. All 16 reference encodings match the format interpreter; fixed mdcomp decodes all 16. |
| Okumura-derived Saxman vs Sega zero-fill semantics | Its ring-buffer interpretation differs; two encoder outputs are wrong for Sega's documented format | A negative source requires a whole-token zero fill, even if the conceptual match crosses address zero. The C codec instead crosses into previous output. mdcomp, ClownLZSS, and the shipped 68k grammar agree on whole-token filling. |
| ClownLZSS Saxman zero-fill at the full-window distance | ClownLZSS decoder disagrees with the documented rebasing/68k target | Modulo arithmetic maps distance 4096 to zero, and `Copy(0, count)` reads uninitialized buffer bytes. Two direct fixtures fail; their scope is the rebasing target, not undefined self-references in the original Z80 routine. |
| ClownLZSS Saxman unused final descriptor | Reference decoder rejects a valid sized stream | It writes the correct 16 output bytes, then attempts another input read and returns an I/O failure instead of stopping at the declared body boundary. |
| KENSSharp sized Saxman | Reference encoder is wrong | Four all-zero cases throw before producing a file: zero-match search reads beyond the remaining input. Twelve other cases write an invalid length using reversed subtraction. Fixed mdcomp rejects the oversized declarations. |
| Primary RC3 Enigma, all-zero inputs | Reference encoder is wrong | Its stream encodes one word more than the input. The format interpreter and both decoders agree on that longer output; look-ahead reads are not bounded by the input length. |
| KENS alpha Enigma, ascending/random inputs | Reference execution failure | Eight cases abort on an out-of-range vector access. Source reads `unpack[pos + 1]` before testing its bound. Empty/absent output is not a valid alternative encoding. |
| Primary RC3 Nemesis, ascending/random inputs | Reference execution failure; no contrary format verdict | Eight calls terminate with access violations and no output file. The mdcomp encodings match the format interpreter and are accepted by the original decoder and both ClownNemesis modes. The precise historical memory defect has not been isolated. |
| Different valid Kosinski/Nemesis encodings | Neither encoder is wrong merely because bytes differ | Match selection and code-table construction are not canonical. Sega-output reproduction, format correctness, and compression quality are separate claims. |

The old external audit is retained as historical evidence, including its mdcomp failures. The new `*-after-saxman` entries in [external-audit.json](external-audit.json) record the repaired candidate. A successful decode of malformed KENSSharp data in the older run was permissive error handling, not evidence that its header was valid.

## Small reproductions

Saxman `04 00 01 41 ED F0` means: four body bytes; one literal `41`; then a three-byte match whose rebased source is -1. Sega semantics produce `41 00 00 00`. The Okumura-derived decoder produces `41 00 41 00`. The hand-constructed fixture `saxman/zero-fill-crossing-origin` records the former. The [shipped decoder](../../src/asm/Saxman.asm) branches on the initial rebased address and fills the complete count; it does not reevaluate the source after each zero.

Saxman `03 00 00 EE F0` requests three zeros at the initial destination. The source is one complete 4096-byte window behind it. ClownLZSS's [decoder](https://github.com/Clownacy/clownlzss/blob/8055bd2e2d258dab604ea26ded9e42144562db54/decompressors/saxman.h) computes a modulo distance of zero, misses the zero-fill branch, and copies unwritten history. This is an implementation defect, not a different valid parse. The observed bytes can vary between runs because that history is uninitialized.

That full-window alias is qualified against the documented rebasing algorithm and 68k target. The original Z80 routine does not perform the same rebasing: an initial source equal to its destination would read unwritten memory rather than guarantee zeros. The fixture is therefore not claimed portable to that original Z80 implementation. In contrast, the negative-origin crossing example above takes the whole-token zero-fill branch in the published Z80 code too. This distinction prevents a target-specific limitation from being mislabeled a universal format rule.

The mdcomp boundary regression uses two groups of eight literals followed by a final unused descriptor: the body is 19 bytes, the little-endian header is `13 00`, and expected output is exactly 16 bytes. It is tested with and without a header, with trailing bytes, and with exact consumed-input assertions. The old decoder emitted extra bytes; the repaired decoder consumes exactly the declared frame. Short headers, short bodies, and an incomplete dictionary token return failure without emitting decoded data.

There is also an error in the Saxman description: its introduction specifies little-endian framing, and its example starts `03 00`, but the example's prose calls it big-endian. The bytes and original Z80 load sequence resolve that contradiction in favor of little-endian. The format description is evidence that must itself be checked, not an infallible oracle.

## Clownacy references and coverage

Accurate Kosinski is a C reconstruction intended to reproduce Sega's compressor, including historical choices; this audit does not execute the subsequently released original LZEXE compressor. The Saxman C source is Clownacy's zero-history adaptation of Okumura's 1989 source, preserved under `p2bin/lz_comp2`; upstream describes Sega-output reproduction. It is not a recovered Sega-specific source file. Current ClownLZSS uses a C90 compression core with C++20 codec headers/frontend, so it must not be described as an entirely C implementation at this pin.

| Reference | Synthetic self/cross cases | All four directions pass | Direct decoder fixtures |
| --- | ---: | ---: | ---: |
| Accurate Kosinski | 56 | 56 | 14/14 |
| Okumura-derived Saxman | 56 | 54 | 5/6 |
| ClownLZSS | 160 | 160 | 17/20 |
| ClownNemesis improved mode | 16 | 16 | 4/4 |
| ClownNemesis Sega-accurate mode | 16 | 16 | Same decoder as improved mode |

The extended Kosinski/Saxman corpus includes sizes around 8/16 descriptor bits and 256-byte match boundaries, seeded-history crossings, and references crossing 4096/8192-byte windows. Other ClownLZSS checks cover Comper, Kosinski Plus, and Rocket with the original 16-case corpus. The independent format interpreters cover Kosinski/Saxman in that matrix; the other three have self/cross-decode evidence only. This does not establish all legal token combinations, optimal compression, moduled reference compatibility, or assembly execution.

Thirty-eight qualified external encodings are saved for offline C++ tests. They require all four self/cross-decodes to pass before capture. Disputed encodings are kept as audit evidence, not mislabeled known-good fixtures. The explicit negative-origin fixture independently specifies the intended Sega behavior.

## Reproduce

Build the exact sources/dependencies listed in `external.json` with CMake/Ninja and GCC. Accurate Kosinski requires its recorded `clowncommon` commit at `lib/clowncommon/clowncommon`; ClownNemesis requires its different recorded commit at `clowncommon`. Build original Saxman with `gcc -std=c90 -O2 -I build-reference/original-saxman tests/support/original_saxman.c build-reference/original-saxman/LZSS.c -o build-reference/original-saxman/original-saxman.exe`. That wrapper supplies body-length framing and bounded input callbacks without changing the codec.

`compare_external.py` accepts `accurate-kosinski`, `original-saxman`, `clownlzss`, `clownnemesis`, and `clownnemesis-accurate` as reference kinds. Append `--boundaries` to add the extended Kosinski/Saxman inputs. For example:

```sh
python tests/support/compare_external.py --reference-kind clownlzss --reference-dir build-reference/clownlzss-build --candidate-dir build-tests --boundaries --output-dir build-reference/clownlzss-boundaries
python tests/support/audit_formats.py --fixture-dir build-tests/tests/fixture-export --comparison-dir build-reference/clownlzss-boundaries --decoder clownlzss build-reference/clownlzss-build --output build-reference/format-audit/report.json --summary build-reference/format-audit/summary.json
```

The fixture export must first be produced with `mdcomp-fixture-export` or the fixture-manifest CTest. Repeat `--comparison-dir` and `--decoder KIND DIRECTORY` for other references. Full traces remain below the build directory; the optional compact summary is suitable for review. The interpreter command fails if published examples or the hand-constructed expectations disagree. Known third-party disagreements remain explicit report results and are not silently turned into passing decoder assertions. No ROM-derived corpus is required for these checks.
