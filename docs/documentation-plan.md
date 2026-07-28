# Documentation Plan

Status: partially independent; format research may proceed now, new-codec
documentation is coordinated with implementation, and final public API
documentation is blocked by the buffer refactor.

> [!IMPORTANT]
> This plan has three dependency tracks:
>
> - Existing codec theory, format notes, and current command-line tool
>   documentation can be developed independently.
> - New codec format and implementation notes are shared deliverables with
>   [`codec-implementation-plan.md`](codec-implementation-plan.md) and must be
>   written alongside their tests and implementations.
> - Final C++ and C usage documentation must describe the stable
>   buffer-oriented interfaces from
>   [`buffer-refactor-plan.md`](buffer-refactor-plan.md), rather than documenting
>   temporary APIs that will immediately be replaced.

The documentation should distinguish four questions:

1. What compression model or theory underlies the codec?
2. What exact byte or bit format does the decoder accept?
3. Which encoding algorithm and trade-offs does mdcomp implement?
4. How does a user invoke the library or command-line tool correctly?

Keeping these answers separate avoids confusing a format requirement with an
implementation choice or an optimality claim.

## Goals

1. Explain the underlying compression algorithm and theory of every codec.
2. Specify each encoded format precisely enough to implement an independent
   decoder.
3. Document mdcomp-specific encoder algorithms and choices where the format
   does not dictate a single approach.
4. State proven, oracle-tested, heuristic, compatibility, and unknown
   optimality claims accurately.
5. Provide task-oriented C++ and C examples after the buffer-oriented APIs are
   stable.
6. Document every installed compression tool, its modes, codec-specific
   options, and file-format conventions.
7. Document how to integrate and call the shipped MC68000 and Z80 assembly
   decoders, with a complete lifecycle guide for the moduled decompression
   queues.
8. Keep documentation consistent with tests, public headers, tool help,
   assembly sources, and released behaviour.

## Scope and Dependencies

| Workstream | Can start now? | Dependency or shared gate |
| --- | --- | --- |
| Existing codec theory and wire-format notes | Yes | Validate against existing code, assembly decoders, and independent fixtures |
| Existing mdcomp implementation notes | Yes | Update when an encoder algorithm changes |
| New codec theory and format notes | Yes, as research | Complete jointly with the corresponding codec workstream and TDD edge manifest |
| New codec implementation notes | During implementation | Record the selected algorithm, alternatives, proof or oracle evidence, and complexity |
| Current command-line tools | Yes | Recheck names and options when new tools or unified tooling are added |
| Existing assembly decoder bindings | Yes | Validate calling conventions and compatibility through the assembly harness in the testing plan |
| Moduled assembly queue guide | Yes, beginning with source audit | Final examples depend on the Mega Drive hardware shim and moduled assembly tests |
| Final C++ API guide | No | Stable codec facade, managed buffers, result types, and moduled APIs from the buffer refactor |
| Final C API guide | No | Stable C ABI, ownership, view, error, and per-codec option contracts from the buffer refactor |

Documentation work must not block independent testing-framework or
buffer-refactor implementation unless a checklist in those plans explicitly
requires the document as an acceptance artifact. Conversely, an API example
must not be finalized against an interface that the buffer plan still marks as
unresolved.

## Documentation Structure

Use a structure similar to:

```text
README.md
docs/
  index.md
  concepts/
    compression.md
    lzss.md
    optimality.md
    moduled-formats.md
  formats/
    comper.md
    comperx.md
    enigma.md
    kosinski.md
    kosinski-plus.md
    lzkn1.md
    nemesis.md
    rocket.md
    saxman.md
    snkrle.md
    ...
  implementation/
    enigma.md
    nemesis.md
    ...
  api/
    cpp.md
    c.md
    migration.md
  assembly/
    index.md
    m68000.md
    z80.md
    moduled-queues.md
  tools/
    index.md
```

This is a proposed information architecture, not a requirement that every
short note be a separate file. A codec with a straightforward implementation
may keep its format and mdcomp implementation sections together. A codec such
as Nemesis or Enigma should have a dedicated implementation note when the
encoding strategy, state model, or optimality discussion would obscure the
wire-format specification.

The README should become a concise landing page: project purpose, supported
codec matrix, build/install quick start, one tool example, one library example,
and links into the detailed documentation. It should not duplicate every
format or API reference.

## Reference Inventory

Maintain a reviewed reference inventory for shared compression research and
link more specific sources from each codec note:

- [Accurate Kosinski compressor](https://forums.sonicretro.org/threads/accurate-kosinski-compressor.40558/)
  (discussion started by Clownacy in 2021; accessed 2026-07-28) is a Kosinski
  compatibility and implementation reference. Use it when documenting
  byte-accurate reproduction of Sega's original compressor, including its
  effective match limits, ring-buffer end-of-input behaviour, dummy matches,
  padding observations, and differences between shortest-output and
  reference-accurate encoding. Treat forum hypotheses separately from behavior
  demonstrated by the referenced implementation and corpus.
- [Sega Retro: Data compression](https://segaretro.org/Category:Data_compression)
  (accessed 2026-07-28) is a discovery index for Sega compression formats,
  terminology, historical context, and format-specific pages. Link the
  relevant individual page from each codec note and corroborate normative
  field or algorithm claims with the shipped decoders, independent
  implementations, fixtures, or other primary evidence.
- [I found the Saxman compressor source code](https://forums.sonicretro.org/threads/i-found-the-saxman-compressor-source-code.39261/#post-959463)
  (discussion started by Clownacy in 2020; accessed 2026-07-28) is a Saxman
  provenance, implementation, and reference-accuracy source. Use the recovered
  original compressor and the discussion around it to document Sega's encoder
  algorithm, deterministic choices, quirks, and the distinction between a
  shortest valid Saxman stream and byte-accurate reproduction. Audit the
  recovered source's provenance and license before copying or deriving
  implementation code; independently validate behavioural claims against
  fixtures and the shipped assembly decoder.
- [KENSSharp](https://segaretro.org/KENSSharp) (accessed 2026-07-28) is a
  stable C# reference implementation based on an earlier mdcomp master-branch
  codebase, before the later C++20 and large-scale refactors. Use a pinned
  release or commit to generate known-good compressed fixtures from
  deterministic synthetic inputs and to audit the current branch for
  behavioural regressions. Its shared lineage with mdcomp must be recorded:
  KENSSharp is a valuable cross-version regression oracle, but is not fully
  independent evidence that the shared algorithm or format interpretation is
  correct. Cross-check its output with the shipped assembly decoders and, where
  available, the accurate Kosinski and recovered Saxman encoders.

For every external reference, record its author or maintainer where known,
retrieval date, license or quotation constraints, which claims it supports,
and whether it is normative evidence, an independent implementation, a
differential oracle, historical context, or only a lead for further research.

Checklist:

- [ ] Add the Kosinski accuracy thread to the Kosinski format and
      implementation-note bibliography.
- [ ] Reconcile its compatibility quirks with the selected mdcomp accurate
      mode, tests, and documented optimality claims.
- [ ] Inventory the relevant codec pages reachable from Sega Retro's data
      compression category.
- [ ] Add specific Sega Retro pages to individual codec bibliographies rather
      than citing only the category index.
- [ ] Add the recovered Saxman compressor thread to the Saxman format and
      implementation-note bibliography.
- [ ] Audit the recovered source's provenance and license, then extract and
      test its documented encoder choices and compatibility quirks.
- [ ] Record and pin the KENSSharp version used to generate synthetic
      reference fixtures.
- [ ] Document KENSSharp's mdcomp lineage, supported codecs and variants, and
      any behaviour that differs from the current branch.
- [ ] Link the current-branch encoder audit and its results from the applicable
      codec implementation notes.
- [ ] Classify factual results, implementation evidence, historical accounts,
      and unresolved forum hypotheses separately.

## Shared Compression Theory

Document shared concepts once and link to them from individual codecs:

- literals, runs, dictionary matches, distances, lengths, and overlapping
  back-references;
- sliding windows and LZSS descriptor streams;
- bit and byte order;
- prefix codes and code-table cost;
- shortest-path or dynamic-programming parsing;
- stateful parse graphs;
- headers, terminators, alignment, and padding;
- split descriptor and parameter/data streams;
- moduled containers;
- the difference between optimal token selection for fixed costs and global
  optimality when tables, states, or token costs interact.

The optimality note must define the terms used throughout the project:

- **format-correct:** emits a stream accepted by conforming decoders;
- **compatible or accurate:** reproduces a specified reference encoder's
  decisions where promised;
- **greedy or heuristic:** uses local or bounded choices without a global
  proof;
- **shortest-path optimal:** minimizes the declared edge costs for a fixed
  graph and format model;
- **oracle-verified:** matches an exhaustive solver within a documented input
  bound;
- **globally optimal:** has a proof or exact practical solver covering the
  supported input domain.

Do not describe every LZSS variant as "perfect compression" without stating
the cost model, supported edge set, header and terminator costs, padding, and
tie-breaking included in that claim.

Checklist:

- [ ] Write the common compression terminology and data-model overview.
- [ ] Write the shared LZSS, descriptor-stream, and overlap explanation.
- [ ] Document graph parsing, edge costs, deterministic tie-breaking, and
      format-level rounding.
- [ ] Document prefix-code and stateful-graph concepts needed by Nemesis and
      Enigma.
- [ ] Define the project's optimality vocabulary and evidence requirements.
- [ ] Document basic and moduled format boundaries without duplicating the
      buffer API guide.

## Per-Codec Format and Theory Notes

Each codec note should separate normative format behaviour from encoder
strategy and include, as applicable:

- intended data unit and common use;
- underlying compression family and shared theory links;
- byte order, bit order, and field diagrams;
- header and representable size ranges;
- token or edge grammar;
- literal, run, dictionary, packed, and control forms;
- history initialization, overlap, wraparound, and zero-fill behaviour;
- terminator and trailing-input rules;
- alignment, logical padding, file padding, and moduled variants;
- decompression algorithm or state machine;
- malformed and truncated input handling;
- worked encode and decode examples with small byte sequences;
- known variants and compatibility constraints;
- provenance and licensing of specifications, assembly, tools, and fixtures.

The note should be precise enough that its token inventory can be compared
directly with the testing plan's format-edge manifest. Field diagrams and
examples should use the same names as the implementation and tests where that
does not compromise clarity.

Initial existing-codec inventory:

- Comper and moduled Comper;
- ComperX and moduled ComperX;
- Enigma;
- Kosinski and moduled Kosinski;
- Kosinski Plus and moduled Kosinski Plus;
- LZKN1 and moduled LZKN1;
- Nemesis;
- Rocket and moduled Rocket;
- Saxman and moduled Saxman;
- SnKRLE and moduled SnKRLE.

New-codec notes share their research and acceptance gates with the codec
implementation plan:

- C42 art;
- Gunstar Heroes;
- Streets of Rage 2;
- SLZ and SLZ24;
- UFTC15 and UFTC16;
- Batman & Robin;
- Compile/Puyo Puyo;
- Kid Chameleon;
- Space Harrier II.

Checklist:

- [ ] Create a documentation matrix covering every existing and planned codec.
- [ ] Reconcile each existing format note with its C++ and assembly decoders.
- [ ] Link every format's token inventory to its test edge manifest.
- [ ] Add at least one small worked stream for each codec.
- [ ] Record ambiguity explicitly instead of presenting an inference as a
      confirmed format rule.
- [ ] Require a normative format note before a new decoder workstream is
      considered complete.
- [ ] Keep moduled-container rules in a shared note and document only
      codec-specific differences on each codec page.

## mdcomp Implementation Notes

Every codec page should contain at least a short statement of how mdcomp
encodes it. Add a dedicated implementation note when the choice is
non-obvious, stateful, heuristic, compatibility-sensitive, or materially
different from the decoder model.

Implementation notes should cover:

- parse state and candidate edge generation;
- objective function and all costs included in it;
- deterministic tie-breaking;
- retained prepare/commit plan data;
- time and memory complexity;
- encoder options and compatibility modes;
- malformed-input validation that is stricter than the wire format;
- known counterexamples or limitations;
- proof, exhaustive oracle, differential, and corpus evidence supporting any
  optimality or compatibility claim;
- alternatives considered and why the selected approach was chosen.

Priority notes include:

- **Enigma:** state representation, interactions between inline values, runs,
  increments, common values, and any exact or heuristic parsing boundary.
- **Nemesis:** nibble-run tokenization, code-table construction, table cost,
  escape handling, the interaction between token and code choices, and the
  precise acceptance level reached by the optimizer.
- **SnKRLE and other greedy/stateful encoders:** whether local choices are
  proven optimal, oracle-verified only for bounded inputs, or heuristic.
- **LZSS variants:** shared shortest-path implementation, codec-specific edge
  costs, descriptor rounding, terminators, and deterministic tie-breaking.
- **Compatibility encoders:** decisions intentionally matching an original
  game or reference tool rather than selecting the shortest stream.

Checklist:

- [ ] Audit the current encoder strategy and optimality claim for every
      existing codec.
- [ ] Write dedicated Enigma and Nemesis implementation notes.
- [ ] Document the shared LZSS parse algorithm once and link codec-specific
      cost models to it.
- [ ] Record complexity and retained-plan memory costs.
- [ ] Link claims to tests, oracle bounds, corpus results, or proofs.
- [ ] Update an implementation note in the same change that materially alters
      its encoder algorithm or public option semantics.

## C++ API Guide

Write the final guide after the buffer refactor stabilizes the public facade.
It should be task-oriented rather than merely reproducing declarations.

Cover:

- installed headers, CMake targets, and linking static or shared builds;
- codec discovery and the supported codec/variant matrix;
- borrowed input, fixed output, owned/resizable output, and mapped-file use;
- exact-size preparation and commit behaviour;
- `codec_result`, status handling, error offsets, and required sizes;
- alignment and input normalization;
- buffer ownership, locks, view lifetime, mutation rules, and thread safety;
- basic and moduled encoding and decoding;
- codec-specific option types;
- logical output size versus file or module padding;
- consumed-input reporting and trailing data;
- legacy stream/file compatibility adapters and their migration path.

Examples should include:

1. encode from a span into library-owned output;
2. decode into an exact fixed buffer;
3. handle `output_too_small` without a partial write;
4. use a codec with options;
5. encode and decode a moduled format;
6. inspect a result and read view safely;
7. migrate one representative legacy stream call.

Checklist:

- [ ] Freeze the guide's terminology against the final public headers.
- [ ] Add minimal build, link, encode, decode, error, and moduled examples.
- [ ] Explain ownership and lifetime at every example boundary.
- [ ] Document all codec-specific C++ option types.
- [ ] Add a legacy API migration table.
- [ ] Compile and run documentation examples in the test build.

## C API Guide

Write this guide against the completed C ABI from the buffer refactor. It must
be usable by a C caller without referring to C++ ownership or exception
semantics.

Cover:

- header inclusion, symbol visibility, linking, and ABI/version expectations;
- opaque buffer creation and destruction;
- borrowed input, borrowed fixed output, resizable output, and mapped buffers;
- read-view creation, access, lifetime, and destruction;
- per-codec encode/decode functions and option structures;
- status and structured diagnostic handling;
- exact required-size retry patterns;
- nullability, ownership transfer, aliasing, and thread-safety rules;
- moduled operations;
- logical size, consumed input, alignment, and padding;
- cleanup on every success and failure path.

Examples should include both a complete success path and an undersized-output
retry path. They must compile as C, not as C++, and should use a single cleanup
section or another pattern that makes ownership unambiguous.

Checklist:

- [ ] Document every opaque type's owner and destruction function.
- [ ] Add a status/diagnostic reference with recovery guidance.
- [ ] Add complete encode, decode, fixed-buffer retry, and read-view examples.
- [ ] Document every per-codec C option structure.
- [ ] Compile examples with the project's supported C compilers.
- [ ] Add at least one installed-library C documentation smoke test.

## Assembly Decoder Bindings

Document the shipped assembly decoders as platform integration interfaces.
These are distinct from the C ABI: they have processor register calling
conventions, assembler-time configuration, memory-layout requirements, and in
some cases Mega Drive hardware dependencies rather than language-level
ownership types.

The assembly index should inventory:

- direct MC68000 decoders for Comper, ComperX, Enigma, Kosinski, Kosinski Plus,
  Rocket, Saxman, and SnKRLE;
- direct Z80 decoders for Kosinski and Kosinski Plus;
- moduled MC68000 queue bindings for Comper, ComperX, Kosinski, and Kosinski
  Plus;
- the original-derived Nemesis assembly decoder described by the maintainer,
  whose source is not present in the current `src/asm` inventory and must be
  located, added, or explicitly documented as out of tree;
- the relationship between each assembly decoder, C++ codec, command-line
  tool, supported format variant, and required headers or padding.

Some of these routines are highly optimized, hand-crafted implementations
written by mdcomp's author rather than translations of an original game
decoder. Their deployment history also varies: some have been used in
production projects for years without reported bugs, while others have seen
little or no use. Document those facts explicitly without treating absence of
bug reports as proof of correctness.

Track two independent classifications for every assembly routine:

1. **Provenance and lineage**
   - recovered or original implementation;
   - derived from a documented reference implementation;
   - independently hand-crafted implementation;
   - shares design assumptions or authorship with the C++ codec.
2. **Validation evidence**
   - no known external use;
   - limited or unknown deployment;
   - sustained field use, including named projects and dates where known;
   - independently sourced known-answer vectors;
   - differential agreement with another implementation;
   - emulator validation;
   - real-hardware validation.

These are evidence labels, not a single maturity ladder. A heavily field-used
routine may still lack boundary tests, while an unused routine may have strong
formal fixtures. Record known bug reports and fixes separately from the
statement that no reports are known.

Record this initial maintainer assessment as a starting point, not as completed
verification:

| Routine group | Initial assessment | Documentation and test consequence |
| --- | --- | --- |
| MC68000 Comper and ComperX | Almost certainly correct | Record hand-optimization and field-use history; retain full boundary tests |
| MC68000 Kosinski and Kosinski Plus | Almost certainly correct | Record hand-optimization and field-use history; cross-check both direct and moduled streams |
| MC68000 Saxman | Almost certainly correct | Cross-check against the recovered original Saxman compressor and document any accepted trailing-data quirks |
| Enigma and Nemesis | Correct; minor edits of original routines | Diff and document the edits; validate the unchanged and modified paths; resolve the missing in-tree Nemesis source |
| Common moduled framework | Known working | Document established integrations, then validate queue, interrupt/resume, final-module, and DMA behavior in the hardware shim |
| All remaining routines | More suspicious until audited | Treat as provisional and apply the full bootstrap suite before using them as format oracles |

The final matrix should replace broad group confidence with per-file evidence,
including which exact revisions were deployed and for how long.

For every direct decoder, record:

- source filename, entry symbol, processor, assembler dialect, and include
  requirements;
- author, origin, derivation, and relationship to the C++ implementation;
- optimization strategy and any invariants introduced by unrolling, lookup
  tables, alignment assumptions, stack manipulation, or read-ahead;
- deployment history, known downstream users, known bug reports, and last
  audited revision;
- validation evidence and whether the routine is currently qualified as a
  differential oracle;
- input, output, option, and length registers;
- clobbered, preserved, and returned registers and condition codes;
- source and destination alignment and address-space requirements;
- whether the routine returns normally, jumps through a configured register,
  or uses another exit convention;
- source and destination pointer state on return, when guaranteed;
- required size prefix, terminator, initial history, or output prefill;
- assembler-time lookup-table, alignment, and loop-unrolling options;
- maximum read-ahead and write extent where known;
- whether self-modifying code, writable code, interrupts, or a particular
  memory map are assumed;
- the exact mdcomp encoder/tool options that produce a compatible stream;
- known differences from an original game decoder or format variant.

Do not infer undocumented register preservation merely because a register is
not obviously modified in one source revision. Treat the routine's declared
contract, an explicit audit, and executable harness results as the evidence.

Checklist:

- [ ] Create an assembly decoder/codec/architecture compatibility matrix.
- [ ] Add provenance/lineage and validation-evidence fields to the assembly
      matrix.
- [ ] Record which routines are hand-crafted optimized implementations and
      which are recovered or reference-derived.
- [ ] Record field-use history without treating a lack of bug reports as a
      correctness proof.
- [ ] Convert the initial maintainer assessment into per-file evidence and
      resolve the current absence of a Nemesis assembly source.
- [ ] Audit and document the complete calling convention of every direct
      MC68000 decoder.
- [ ] Audit and document the Kosinski and Kosinski Plus Z80 calling
      conventions.
- [ ] Record assembler, include-path, macro, and configuration requirements.
- [ ] Document size-prefix, terminator, alignment, history, and prefill
      requirements for each decoder.
- [ ] Document the compatible C++ encoder options and command-line invocation
      for every assembly routine.
- [ ] Link each contract to the assembly test that exercises it.
- [ ] Apply the same documentation template to future assembly decoders.

### Moduled MC68000 decompression queues

Give the moduled bindings a dedicated integration guide. The current
`Comper_Moduled.asm`, `ComperX_Moduled.asm`, `Kosinki_Moduled.asm`, and
`KosinkiPlus_Moduled.asm` wrappers combine a codec-specific internal decoder
with `Moduled_common_header.asm` and `Moduled_common_footer.asm`. They expose a
cooperative, interrupt-resumable decompression queue and a second queue that
copies completed 4096-byte modules to VRAM through a project-supplied DMA
hook. They must not be documented as ordinary one-call decompression
functions.

The guide should explain the complete data path:

```text
moduled archive in ROM
  -> module queue
  -> codec decompression queue
  -> 4096-byte RAM staging buffer
  -> project DMA queue
  -> consecutive VRAM destinations
```

Document the archive contract in enough detail to match the C++ moduled
encoder:

- the uncompressed-size header and its byte order;
- 4096-byte module boundaries and final partial-module sizing;
- where one compressed module ends and the next begins;
- per-codec terminators and any inter-module alignment;
- the Kosinski 16-byte padding rule versus unpadded Comper, ComperX, and
  Kosinski Plus streams;
- the legacy Kosinski size remapping controlled by
  `module_remap_A000_to_8000`;
- logical decompressed size, DMA transfer size, and VRAM destination
  advancement;
- empty input and exact-boundary behaviour.

Document each queue operation and its scheduling role:

- `Clear_Kos_Queue`;
- `Queue_Kos_Module`;
- `Process_Kos_Module_Queue_Init`;
- `Process_Kos_Module_Queue`;
- `Queue_Kos`;
- `Process_Kos_Queue`;
- `Set_Kos_Bookmark`;
- the save, restore, and resume entry points supplied by the common footer.

The historical `Kos` names are shared by all four wrappers; the guide must make
that reuse explicit so users do not mistake the Comper or Kosinski Plus queues
for Kosinski-format data.

Provide a frame-level usage sequence showing:

1. allocate and initialize all queue state and the staging buffer;
2. call `Clear_Kos_Queue` during initialization;
3. enqueue an archive source and VRAM destination;
4. service the module and decompression queues in the required order;
5. call the bookmark hook from the correct interrupt path if resumable
   decompression is enabled;
6. allow the project DMA queue to transfer a completed module;
7. detect completion and safely reuse the archive or destination state.

The final calling order must be verified against executable tests rather than
deduced only from symbol names.

Inventory every external dependency and integration obligation, including:

- caller-provided queue arrays, queue-end symbols, counters, source and
  destination slots, saved-register areas, bookmark storage, and the 4096-byte
  `Kos_decomp_buffer`;
- queue capacities and full-queue behaviour;
- `QueueDMATransfer`, `disableInts`, `dmaSource`, `DMAfunctions_defined`, and
  `AssumeSourceAddressInBytes`;
- whether VRAM and DMA addresses are expressed in bytes or words;
- the expected interrupt stack frame and the fixed offset used by
  `Set_Kos_Bookmark`;
- interrupt masking around DMA queue submission;
- the `KosMSaveRegs` and `KosMRestoreRegs` macro contract for each codec;
- codec-specific fixed registers, lookup tables, continuation registers, and
  loop-unroll state;
- non-reentrancy, shared global state, main-loop/V-int ownership, and safe
  mutation rules;
- ROM/RAM placement, alignment, staging-buffer lifetime, and DMA completion
  ownership.

Include an annotated minimal integration skeleton with project-specific memory
symbols and DMA hooks clearly marked as placeholders. Also include separate
examples for direct-to-RAM queue use and the full moduled-to-VRAM pipeline so
users do not need to adopt the DMA layer when they only need incremental
decompression.

Checklist:

- [ ] Document the exact moduled archive layout accepted by the assembly
      queues and emitted by each matching C++ moduled encoder.
- [ ] Document all public queue functions, registers, state transitions, and
      completion conditions.
- [ ] Inventory every required RAM symbol, its size/alignment, and queue
      capacity.
- [ ] Document the DMA, interrupt, bookmark, address-unit, and platform macro
      contracts.
- [ ] Document codec-specific save/restore sets and continuation state.
- [ ] Add a queue state diagram and one frame-by-frame scheduling example.
- [ ] Add an annotated minimal assembly integration skeleton.
- [ ] Add separate direct-to-RAM and moduled-to-VRAM examples.
- [ ] Explain the 4096-byte staging requirement, final partial transfer, and
      Kosinski-only padding/remapping behaviour.
- [ ] Cross-check every archive example with the C++ moduled encoder.
- [ ] Validate queue ordering, interruption/resume, DMA parameters, multiple
      archives, queue-full handling, and completion in the testing plan's
      Mega Drive hardware shim.

### General assembly codec notes

Each codec's format page should contain a short assembly section even when no
moduled queue exists. It should state:

- which processor implementations are shipped;
- the decoder entry point and a link to the full binding contract;
- whether compressed size is stored, passed in a register, or found through a
  terminator;
- format variants selected by entry point or assembler configuration;
- required initial values such as Enigma's starting pattern name;
- special history, destination, or prefill assumptions;
- important performance-oriented implementation choices that affect
  integration but not the format;
- whether the assembly decoder is independent enough to serve as a
  differential oracle for the C++ encoder;
- its provenance class, deployment evidence, validation evidence, and current
  oracle qualification;
- any known interoperability limitation.

The codec page should not repeat full register tables or queue integration
instructions. Those belong in the assembly binding guides and should be linked
from the codec note.

## Compression Tool Guide

Document the installed tools as user-facing products, not just thin wrappers
around the library. The current executable names are:

- `compcmp`;
- `comperx`;
- `enicmp`;
- `koscmp`;
- `kosplus`;
- `lzkn1cmp`;
- `nemcmp`;
- `rockcmp`;
- `saxcmp`;
- `snkcmp`.

Start with one shared guide for common behaviour and a capability table for
codec-specific differences. Split out per-tool pages only when their options
or data conventions need substantial explanation.

Cover:

- installation and how executable names map to codecs;
- default compression, extraction/decompression, and recompression modes;
- input and output filename handling;
- source offsets/pointers;
- moduled mode and module padding;
- explicit decompressed-size and stored-size options;
- compressed-end reporting;
- codec-specific switches and format variants;
- exit statuses and diagnostic output;
- overwrite and in-place recompression behaviour;
- shell examples on supported platforms;
- how file padding differs from logical compressed data;
- how to identify which tool and options produced a fixture.

The guide must be audited against each tool's actual option type. Not every
tool supports every common-looking option, and the documentation must not
imply otherwise. Tool help text should remain the concise command-line source
of truth; the guide should explain workflows, consequences, and examples that
do not fit comfortably in `--help` output.

Checklist:

- [ ] Inventory each current executable, installed name, modes, and options.
- [ ] Add a tool-to-codec and option-capability matrix.
- [ ] Document compress, extract, recompress, moduled, offset, size, and
      information workflows where supported.
- [ ] Add examples using small redistributable or generated inputs.
- [ ] Document exit statuses, failure behaviour, overwrite rules, and padding.
- [ ] Add CLI smoke tests for documented invocations.
- [ ] Update the guide and matrix whenever a codec tool or option is added,
      removed, or renamed.
- [ ] Document any future unified tool without deleting the individual-tool
      migration guidance prematurely.

## Documentation Validation

Documentation should be checked as an interface:

- compile and run C++ examples;
- compile C examples with a C compiler;
- assemble documented MC68000 and Z80 integration examples;
- execute direct and moduled assembly examples in the testing plan's emulator
  harnesses;
- execute documented CLI commands against generated fixtures;
- validate byte diagrams and worked examples against known-answer tests;
- validate codec token inventories against format-edge manifests;
- check internal links and headings;
- identify the mdcomp release or source version to which API docs apply;
- mark planned, experimental, and unavailable features explicitly.

Prefer snippets sourced from tested examples or shared with tests where this
does not make the prose unreadable. Avoid manually duplicating long option
tables if a small generator can derive them deterministically from stable
metadata, but do not make the documentation build depend on executing target
binaries.

Checklist:

- [ ] Add link and Markdown style checks.
- [ ] Add C++ and C documentation-example targets under `BUILD_TESTING`.
- [ ] Assemble and execute the documented direct and moduled binding examples
      through the reusable assembly harnesses.
- [ ] Add deterministic CLI documentation smoke tests.
- [ ] Cross-check format diagrams and examples with fixtures.
- [ ] Add a documentation review item to codec, API, and tool pull-request
      templates or contributor guidance.
- [ ] Fail CI when a tested documentation example no longer compiles or
      produces the documented result.

## Milestone Checklist

### 0. Structure and inventory

- [ ] Create the documentation index and proposed directory structure.
- [ ] Replace the README TODOs with links to tracked documentation work.
- [ ] Create codec, API, and tool coverage matrices.
- [ ] Define terminology, style, field-diagram, and provenance conventions.

### 1. Independent existing-codec documentation

- [ ] Write the shared compression, LZSS, and optimality notes.
- [ ] Document the wire format and decompression model of every existing codec.
- [ ] Document the current encoder strategy of every existing codec.
- [ ] Complete the detailed Enigma and Nemesis implementation notes.

### 2. Current tools

- [ ] Publish the common command-line workflow guide.
- [ ] Publish the tool/codec/option capability matrix.
- [ ] Add tested examples for every supported tool mode.

### 3. Assembly decoder bindings

- [ ] Publish the direct MC68000 and Z80 calling-convention reference.
- [ ] Publish provenance, optimization, field-use, bug-history, validation,
      and oracle-qualification metadata for every routine.
- [ ] Publish the moduled archive, queue-state, interrupt, staging-buffer, and
      DMA integration guide.
- [ ] Add tested direct-to-RAM and moduled-to-VRAM integration examples.
- [ ] Add general assembly compatibility notes to every applicable codec page.

### 4. New codec workstreams

- [ ] Complete each new codec's normative format note with its edge manifest.
- [ ] Complete its implementation note with its encoder.
- [ ] Publish its API options and tool additions during integration.

### 5. Buffer-oriented public APIs

- [ ] Publish the C++ guide after the codec facade and managed-buffer API land.
- [ ] Publish the C guide after the ABI and option structures land.
- [ ] Publish the legacy stream-to-buffer migration guide.
- [ ] Compile and run every public API example.

### 6. Release integration

- [ ] Replace the README's incomplete codec information with the support
      matrix and quick starts.
- [ ] Include documentation in install/package rules where appropriate.
- [ ] Run link, example, and CLI documentation checks in CI.
- [ ] Review all optimality, compatibility, and stability claims before
      release.

## Completion Criteria

This plan is complete when:

- every supported codec has a reviewed theory and wire-format note;
- every non-trivial encoder has an accurate implementation/optimality note;
- every new codec's documentation agrees with its implementation plan,
  format-edge manifest, and fixtures;
- the stable C++ and C APIs have tested task-oriented guides;
- every shipped assembly decoder has an audited calling convention,
  compatibility note, provenance classification, optimization summary,
  deployment history, and validation evidence;
- the moduled assembly queues have a tested lifecycle, interrupt, RAM-state,
  staging-buffer, and DMA integration guide;
- every installed compression tool and supported mode is documented with a
  tested example;
- the README provides an accurate entry point and codec matrix;
- documentation examples, links, and CLI workflows are validated in CI;
- no planned or heuristic behaviour is presented as implemented or proven.

## Deferred Decisions

- Whether to generate declaration-level API reference with Doxygen or another
  tool in addition to the task-oriented guides.
- Whether short format and implementation notes remain combined or are split
  consistently into separate directories.
- Whether the command-line capability matrix is maintained manually or
  generated from stable option metadata.
- Whether a future unified compression tool supplements or replaces the
  individual executables.
- Whether the shared moduled queue receives codec-neutral public symbol aliases
  in addition to its historical `Kos` names.
- Whether a hardware-independent incremental moduled decoder is added alongside
  the current Mega Drive DMA-oriented queue.
- Which documentation artifacts are installed with binary packages versus
  published only with source or on a documentation site.
