# Assembly test dependencies

These pinned submodules are used only when `BUILD_TESTING=ON` and `MDCOMP_ASSEMBLY_TESTS=ON`. Initialize them with `git submodule update --init --recursive`. Normal configuration never downloads them, and missing sources produce an actionable configuration error.

| Submodule | Commit | Role and license |
| --- | --- | --- |
| `asl` | `41c70d8895daf1a16ca3c3a591477f064a7030bb` | GPL-2.0 assembler and p2bin host tools, executed separately; only `M68K;Z80` CPU families enabled |
| `musashi` | `313ebf1bd9f4d0d93341eb5ce21fd8a119e9dbdd` | Permissively licensed Musashi MC68000 core, privately linked into tests |
| `z80` | `d64fe10a2274e5e40019b1086bf7d8990cbc5f23` | MIT-licensed superzazu/z80 C core, privately linked into tests |

Each dependency retains its upstream license and attribution. ASL is built in its own build directory and does not contribute object code to mdcomp's libraries. The emulator targets are warning-isolated and are not installed or exported. The MC68000 configuration disables later CPU variants and enables original-CPU address-error behavior.

The optional original-derived Sega routines are separately imported into an ignored directory with `tests/support/import_sega_decoders.py`; they are not submodules or vendored files. [Their manifest](../references/sega-decoders.json) records exact source/block hashes and licensing qualifications. See [the assembly workflow](../asm/README.md) for setup, execution, provenance, and remaining limitations.
