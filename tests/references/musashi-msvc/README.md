# MSVC x64 miscompilation of Musashi's opcode generator

The failure is an MSVC optimizer defect reproduced independently of the rest of Musashi, rather than a demonstrated undefined-behavior defect in the generator's bit-pattern loop. MSVC 19.44.35229.0 targeting x64 with `/O2` loses the contribution of iteration 1 when unrolling the `unsigned short` compound-OR accumulations. The exact internal compiler pass responsible has not been identified. Newer MSVC versions have not been tested.

The affected upstream revision is `313ebf1bd9f4d0d93341eb5ce21fd8a119e9dbdd`, which was also upstream master when checked. The relevant source is [`populate_table()` in m68kmake.c](https://github.com/kstenerud/Musashi/blob/313ebf1bd9f4d0d93341eb5ce21fd8a119e9dbdd/m68kmake.c#L1144). No matching report was found among upstream's existing issues and pull requests during this investigation.

## Standalone reproduction

[repro.c](repro.c) is a 28-line reduction with no Musashi dependency. From an x64 Native Tools Command Prompt, copy it into a scratch directory and run:

```bat
cl /nologo /TC /std:c11 /W4 /O2 /FAs repro.c /Fe:repro-o2.exe
repro-o2.exe
cl /nologo /TC /std:c11 /W4 /Od repro.c /Fe:repro-od.exe
repro-od.exe
```

Expected output is `mask=f000 match=f000`, with exit status 0. The affected `/O2` build prints `mask=b000 match=b000` and exits with status 1. `/Od` produces the expected result. The command-line reproduction was checked independently of CMake, using the installed Visual Studio 2022 toolset in C11 mode. `/FAs` produces the assembly listing used below. Optional input arguments are only for inspecting other 16-character patterns; the program's exit-status check deliberately expects the default example.

| Compiler and configuration | Standalone result |
| --- | --- |
| MSVC 19.44.35229 x64, `/O2` | Incorrect: `b000/b000` |
| MSVC 19.44.35229 x64, `/Od` | Correct: `f000/f000` |
| MSVC 19.44.35229 x64, `/O1` | Correct: `f000/f000` |
| MSVC 19.44.35229 x64, `/O2 /Os` | Correct: `f000/f000` |
| MSVC 19.44.35229 x86, `/O2` and `/O1` | Correct: `f000/f000` |
| GCC 16.2 x64, `-std=c11 -O2 -Wall -Wextra -Werror` | Correct: `f000/f000` |
| Clang 23.1 x64, `-std=c11 -O2 -Wall -Wextra -Wconversion -Werror` | Correct: `f000/f000` |

The source accesses a valid two-element output array and a string checked to contain 16 characters. Both accumulators are initialized. Each comparison produces an `int` equal to 0 or 1, each shift count is between 0 and 15, and each result fits the tested implementation's 32-bit `int` and 16-bit `unsigned short`. There is no signed overflow or out-of-range shift in this reduction. Sanitizer execution was unavailable because the local Windows toolchains lack a usable UBSan runtime; no sanitizer-clean result is claimed.

## Where the generated instructions go wrong

For the default pattern `1111............`, the mask should progress through `8000`, `c000`, `e000`, and `f000`. In the x64 `/O2` listing, iteration 0 leaves `cx=8000`. Iteration 1 correctly computes `ax=c000` and stores it, but iteration 2 uses the stale `cx=8000` instead of the accumulated `c000`:

```asm
; iteration 1, si=4000h, cx=8000h
cmp     BYTE PTR [r10+1], 46
cmovne  ax, si
or      ax, cx
mov     WORD PTR [rdx], ax       ; stores c000h
; intervening instructions handle the match accumulator
; iteration 2, bp=2000h, cx is still 8000h
cmp     BYTE PTR [r10+2], 46
cmovne  ax, bp
or      ax, cx                   ; becomes a000h, losing bit 14
mov     WORD PTR [rdx], ax
```

The match accumulation has the same stale-value problem. The final result is therefore `b000`. With the full generator's local bit-pattern array, additional optimization removes the now-dead iteration-1 comparisons altogether. Disabling vectorization with `#pragma loop(no_vector)` does not repair the full generator, and `/O2 /Ob0` also remains incorrect. The evidence points to scalar optimization/unrolling rather than an SIMD instruction problem; it does not establish the compiler's internal faulty pass.

## Effect on the complete generator

Build the unmodified upstream source with `cl /TC /std:c11 /O2 m68kmake.c /Fe:m68kmake.exe`, then run `m68kmake.exe output-directory m68k_in.c`. The following generated entries change:

| Handler | Expected mask / match | Incorrect mask / match |
| --- | --- | --- |
| `m68k_op_1010` | `f000 / a000` | `b000 / a000` |
| `m68k_op_1111` | `f000 / f000` | `b000 / b000` |
| `m68k_op_moveq_32` | `f100 / 7000` | `b100 / 3000` |

The corrupted masks affect table sorting and invalidate assumptions in the opcode jump-table builder. In mdcomp's MC68000 calibration this led to an access violation during `m68k_init()`, before executing the calibration program. `/Od` and `/O1` both produce the same complete generated output as GCC.

## Candidate upstream workaround

[reverse-loop.patch](reverse-loop.patch) changes only the loop direction, from `for(i=0;i<16;i++)` to `for(i=15;i>=0;i--)`. The calculation still uses `(15-i)` and ORs exactly the same sixteen contributions, so the mathematical result is unchanged. This avoids the observed miscompilation in the full generator while retaining `/O2`.

With CRLF normalized to LF, the complete `m68kops.c` SHA-256 is `f36a23ff55ba012081b0b28815c23c975782dbca5eb1e10dbaf8825d14bb117e` for GCC, unmodified MSVC `/Od`, unmodified MSVC `/O1`, and the reverse-loop MSVC `/O2` build. The unmodified MSVC `/O2` output instead hashes to `94bf739955f3e982a2b19862773775ff75f347dfce68fe77fffea04f34bae34b`. All builds generated 1967 handlers from 518 primitives.

mdcomp retains its existing `/Od` workaround for the build-time generator; this investigation does not modify the pinned Musashi submodule or reduce optimization of the emulator core. The standalone reduction is suitable for a Microsoft compiler report, while Musashi can use the source-level workaround and add a generator regression check. Reversing the loop is a workaround for the compiler defect, not a correction to the original algorithm.
