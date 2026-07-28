# Testing Framework Plan

Status: prerequisite design; implementation not started.

> [!IMPORTANT]
> This plan blocks implementation of both
> [`buffer-refactor-plan.md`](buffer-refactor-plan.md) and
> [`codec-implementation-plan.md`](codec-implementation-plan.md). Establish the
> regression suite against the current stream-based codecs before changing
> their I/O model, then use the same framework for test-driven implementation
> of new codecs.

The dependency order is:

1. establish the testing framework and capture current codec behaviour;
2. perform the buffer refactor under the regression suite;
3. improve existing codecs and add new codecs using test-driven development.

Research, format documentation, fixture collection, and other non-mutating
design work in the two dependent plans may continue before this plan is
complete. Documentation deliverables and their validation are tracked in
[`documentation-plan.md`](documentation-plan.md).

## Goals

1. Detect behavioural regressions while existing codecs are refactored.
2. Provide a concise TDD workflow for every new codec.
3. Test C++ encoders and decoders independently as well as by round-tripping.
4. Exercise every legal format edge/token type in both encoding and decoding.
5. Assemble and execute the shipped MC68000 and Z80 decoder sources as part of
   automated tests.
6. Qualify each assembly decoder's provenance and validation strength, then
   use it to cross-check C++ encoders without assuming that field use or a
   separate implementation language alone makes it an independent oracle.
7. Keep fixtures deterministic, redistributable, and traceable to their source.
8. Integrate all test layers with CMake, CTest, local development, and CI.

## Current Baseline

The project currently has no CTest integration or runtime test targets. Its
existing checks are compile-time `static_assert`s for concepts, byte order,
bit operations, iterators, and selected adaptor properties. Those assertions
remain valuable, but they do not execute codec operations or validate format
compatibility.

The current implemented C++ codec set to baseline is:

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

The current `artc42` target is a stub and belongs to the new work in the codec
implementation plan. It must use the TDD path, not be treated as a working
baseline codec.

## Test Architecture

### Test runner and build integration

Use CTest as the project-level orchestrator and
[Catch2 v3](https://github.com/catchorg/Catch2) as the initial C++ test
framework. Catch2 provides native CMake targets and
`catch_discover_tests`, while CTest supplies filtering, labels, timeouts,
parallel execution, and a common CI entry point.

Add a standard `BUILD_TESTING` path using `include(CTest)`. Test-only
dependencies must not be required when `BUILD_TESTING=OFF`, must not appear in
installed mdcomp package metadata, and should be supplied through a dedicated
test feature in the existing vcpkg workflow. The emulator cores are C, so the
test build must also enable C as a project language without changing mdcomp's
public C++ API.

Initial CTest labels:

- `unit`: low-level cursor, bitstream, arithmetic, and helper tests;
- `codec`: C++ codec contract and fixture tests;
- `asm-68000`: MC68000 assembly-decoder tests;
- `asm-z80`: Z80 assembly-decoder tests;
- `moduled`: C++ and assembly moduled-format tests;
- `slow`: exhaustive, large-corpus, or long randomized tests.

Checklist:

- [ ] Add `include(CTest)` and make all runtime tests conditional on
      `BUILD_TESTING`.
- [ ] Add Catch2 v3 through a test-only vcpkg manifest feature.
- [ ] Enable the C language for the test-only emulator targets.
- [ ] Create `tests/` with separate support, unit, codec, assembly, and fixture
      directories.
- [ ] Add shared Catch2 test executables without linking every codec into every
      test unnecessarily.
- [ ] Register test cases with CTest using `catch_discover_tests`.
- [ ] Apply deterministic per-test timeouts and CTest labels.
- [ ] Ensure tests write generated files only below the build directory.
- [ ] Preserve and continue compiling the existing `static_assert`s.
- [ ] Add a documented `cmake --build` plus `ctest` local workflow.
- [ ] Provide reusable compile/run hooks for the tested C++, C, and CLI
      examples required by [`documentation-plan.md`](documentation-plan.md).

### Common codec contract

Create a test-only codec descriptor that adapts each public codec API to a
common operation set:

```cpp
struct codec_test_api {
    std::string_view name;
    encode_fn encode;
    decode_fn decode;
    std::optional<encode_fn> moduled_encode;
    std::optional<decode_fn> moduled_decode;
    codec_edge_manifest edges;
};
```

This descriptor is test infrastructure, not a new public codec API. It should
initially wrap the stream interfaces and later wrap the memory-buffer
interfaces from the buffer refactor. Running equivalent contract cases
through both APIs during migration will prove that the compatibility adapters
preserve behaviour.

Checklist:

- [ ] Define a common test-only adapter for basic encode and decode operations.
- [ ] Add optional moduled encode and decode operations.
- [ ] Normalize successful results and diagnostics for assertions.
- [ ] Allow a fixture to express codec options, padding, expected consumed
      input, and expected produced output.
- [ ] Run the same contract suite against every applicable codec.
- [ ] During the buffer refactor, run equivalent stream and buffer operations
      over the same fixtures.
- [ ] Add installed-package smoke tests separately from internal library tests.

## C++ Codec Tests

### Round-trip tests

For every working encoder/decoder pair, verify:

```text
decode(encode(uncompressed)) == uncompressed
```

Round-tripping is necessary but not sufficient: a matching encoder and decoder
can share the same misunderstanding of a format. Round-trip cases therefore
complement, rather than replace, independent known-answer tests.

Each codec should include:

- empty input if the format permits it;
- minimum legal input and alignment;
- literal- or raw-only data;
- highly repetitive data;
- data forcing minimum and maximum match lengths and distances where relevant;
- descriptor-field and bit-buffer boundaries;
- terminator and final-padding boundaries;
- inputs immediately below, at, and above a module boundary;
- a deterministic pseudo-random corpus with recorded seeds.

Checklist:

- [ ] Add basic round-trip cases for every working C++ codec.
- [ ] Add moduled round-trip cases for every codec exposing moduled APIs.
- [ ] Test all public encoder options and format variants.
- [ ] Record deterministic random seeds and print the seed on failure.
- [ ] Save a minimal reproduction artifact below the build directory when a
      generated case fails.
- [ ] Add size and runtime limits so malformed behaviour cannot hang CI.

### Known-answer decoding

For every decoder, store known compressed input and its independently known
decompressed form, then verify:

```text
decode(known_compressed) == known_uncompressed
```

Known-answer vectors should come from published examples, legally
redistributable reference tools, original assembly decoders, or carefully
hand-constructed streams. A vector generated only by the C++ encoder under
test is not independent and must not be labelled a known-answer vector.
Reference-generated vectors with shared mdcomp ancestry may be used as stable
regression fixtures, but their lineage must be recorded and they must not be
presented as independent confirmation of the shared algorithm.

Checklist:

- [ ] Add at least one independent known-answer vector for every decoder.
- [ ] Add hand-constructed vectors for token forms not selected naturally by
      the current encoder.
- [ ] Record provenance, license, format variant, and any transformation for
      every fixture.
- [ ] Verify exact decompressed length and bytes.
- [ ] Verify compressed input consumption where the API exposes it.
- [ ] Add truncated and malformed variants once structured errors are
      available.
- [ ] Do not require byte-for-byte encoded output except for a separately
      documented compatibility or accurate-encoding mode.

### Reference encoders and current-branch audit

Use deterministic synthetic decompressed inputs with pinned reference encoders
to create a known-good compressed corpus without relying on ROM-derived data.
Initial reference encoders are:

- [KENSSharp](https://segaretro.org/KENSSharp), using a pinned release or
  commit from its stable C# implementation;
- the encoder described by the
  [accurate Kosinski compressor](https://forums.sonicretro.org/threads/accurate-kosinski-compressor.40558/)
  research, for reference-accurate Kosinski and moduled Kosinski output; and
- the recovered original encoder from the
  [Saxman compressor source-code discussion](https://forums.sonicretro.org/threads/i-found-the-saxman-compressor-source-code.39261/#post-959463),
  subject to provenance and redistribution review.

KENSSharp is based on an earlier mdcomp master-branch codebase, before the
current branch's extensive C++20 and structural refactors. It is therefore a
useful stable cross-version oracle, but not an algorithmically independent
implementation. Label its fixtures `reference-generated` and record their
shared lineage. A KENSSharp vector can independently exercise the current
decoder without invoking the current encoder, but agreement between the two
does not by itself prove that their inherited format interpretation is
correct.

For each generated fixture, retain:

- the synthetic input bytes or deterministic generator and seed;
- the input's intended edge and boundary obligations;
- reference tool name, source URL, exact release or commit, and license;
- complete command line, codec variant, options, module size, and padding;
- compressed and decompressed sizes and cryptographic hashes;
- whether byte-for-byte compatibility or only valid format output is claimed;
- the independent decoder or assembly routine used for cross-validation.

Prefer committing a small reviewed corpus and its generation metadata rather
than making normal tests download or execute every reference encoder.
Generation should remain reproducible through an opt-in maintenance target or
documented script. Reference encoders do not replace hand-authored streams for
legal decoder-only, dominated, ignored-control, or non-canonical token forms
that they never emit.

Before treating the current branch as the pre-refactor correctness baseline,
audit every current encoder against this corpus:

1. Generate synthetic raw cases covering every encoder edge, minimum and
   maximum parameters, descriptor boundaries, terminators, input granularity,
   module boundaries, and deterministic random seeds.
2. Encode them with each applicable pinned reference encoder and the current
   branch.
3. Decode every reference and current-branch output with each matching shipped
   assembly decoder, recording its provenance and verification classification,
   and compare exact output bytes. A provisional routine is an additional
   subject under test, not sole evidence that the encoder is correct.
4. Decode the reference outputs with the current C++ decoder and verify exact
   output, input consumption, headers, logical size, and padding.
5. Inspect both outputs against the edge manifest; do not infer format
   coverage only from a successful round trip.
6. Require byte equality only for a documented accurate or compatibility mode.
   For shortest-output modes, compare validity, encoded size, declared cost
   model, and deterministic tie-breaking as applicable.
7. Investigate every discrepancy before changing a fixture or blessing current
   behaviour. Record confirmed current-branch bugs as explicit baseline
   failures with minimal reproductions.

The current branch is considered audited only after all supported encoder
variants either pass these checks or have a documented, isolated known failure.
The audit establishes confidence in the baseline; it must not silently convert
a shared KENSSharp/mdcomp bug into expected behaviour.

Checklist:

- [ ] Pin exact KENSSharp, accurate Kosinski, and recovered Saxman reference
      versions and record licenses.
- [ ] Define a deterministic synthetic source corpus from the edge manifests.
- [ ] Generate and review reference-compressed basic and moduled fixtures.
- [ ] Record generator commands, options, versions, lineage, and hashes in each
      fixture manifest.
- [ ] Cross-decode generated streams with the current C++ and shipped assembly
      decoders.
- [ ] Audit every current-branch encoder and public option against applicable
      reference fixtures.
- [ ] Compare exact bytes only where reference-accurate behaviour is promised.
- [ ] Triage and minimize every mismatch before accepting or updating expected
      data.
- [ ] Publish the audit matrix and known failures in the test documentation.

### Format-edge coverage

"100% codec coverage" in this plan means 100% semantic format-edge coverage,
not merely compiler line coverage. Every codec receives a manifest of all
legal encoder edge types and decoder token forms, including relevant subtypes
that select different code paths or parameter encodings.

Track encoding and decoding separately:

- **encoder coverage** proves that some input makes the encoder select and emit
  each edge type it is designed to produce;
- **decoder coverage** proves that the decoder accepts each legal edge type,
  including dominated or non-canonical forms that an optimal encoder may never
  choose;
- **boundary coverage** exercises minimum and maximum parameters, descriptor
  reloads, terminators, padding, headers, and format variants.

Use a test-only token observer or inspector to classify the compressed stream
produced or consumed. Do not infer edge coverage only from decompressed output,
and do not expose instrumentation through the installed public ABI.

Checklist:

- [ ] Inventory the edge/token forms for each existing codec.
- [ ] Define a stable test-only edge identifier for each form.
- [ ] Add a per-codec manifest distinguishing encoder, decoder, and boundary
      obligations.
- [ ] Add test-only observation at a shared layer where possible.
- [ ] Add codec-specific inspectors for formats that do not use the shared
      LZSS layer.
- [ ] Verify that encoded output actually contains every claimed encoder edge.
- [ ] Use hand-authored streams to cover legal decoder-only edge forms.
- [ ] Fail the suite when a manifest obligation has no covering fixture.
- [ ] Require the manifest and initial failing fixtures before implementing a
      new codec.
- [ ] Optionally collect line and branch coverage as a diagnostic, but do not
      substitute it for format-edge coverage.

### Constexpr LZSS adaptor inverse tests

After the buffer refactor replaces the LZSS stream wrappers with span-backed
memory and descriptor cursors, reuse those allocation-free paths in
constant-evaluated adaptor tests. This is a migration-stage addition to the
test suite, not a prerequisite for capturing the current stream-based
regression baseline.

A test helper should:

1. construct a valid encoder-supported edge and an explicit starting
   decompressed position;
2. call `Adaptor::encode_edge` through a fixed-capacity output and descriptor
   cursor, then finalize it;
3. construct the matching input cursor over exactly the produced bytes;
4. call `Adaptor::decode_edge` into a fixed-capacity constexpr-compatible edge
   collector initialized with the same decompressed position; and
5. call `Adaptor::output_edge` for the original and decoded edges using
   separate fixed output cursors with identical initialized history; and
6. compare the produced bytes and output position, as well as the decoded
   semantic edge, decompressed-size change, continuation or termination result,
   and exact compressed input consumption with the encoded case.

Semantic edge equality compares type, symbol or inline data, distance, and
length as applicable. It does not compare incidental storage ownership or the
compressed-stream position currently stored on decoded nodes. Rocket, Saxman,
and any other position-dependent formats must be checked at representative
wrap boundaries as well as at ordinary positions. Dictionary cases must seed
both fixed output buffers with identical deterministic history, including
cases whose match overlaps newly produced output. Format-defined zero-fill
forms should compare their produced bytes rather than a sentinel distance used
only by the encoder's graph.

Require direct `static_assert` checks for every encoder-supported edge form and
its minimum and maximum legal parameter values. Use the same helper in
parameterized Catch2 tests for broader deterministic position and value
coverage. A compile-time assertion should prove
`decode_edge(encode_edge(edge))` semantic equivalence; it must not claim that
all accepted byte sequences round-trip byte-for-byte. Decoder-only,
non-canonical, compatibility, and ignored-control encodings still require
independent known-answer decoder tests.

Checklist:

- [ ] Add a fixed-storage constexpr edge-codec harness after the memory cursor
      migration.
- [ ] Define per-adaptor semantic edge comparison, including `output_edge`
      bytes, output-size, and termination behaviour.
- [ ] Add `static_assert` inverse checks for every encoder-supported LZSS edge
      form at minimum and maximum parameter values.
- [ ] Cover literal, fixed-history dictionary, overlapping dictionary, packed
      literal, zero-fill, and terminator output behaviour as applicable.
- [ ] Exercise position-dependent encodings before, at, and after their wrap
      boundaries.
- [ ] Reuse the helper in runtime parameterized tests for a broader
      deterministic corpus.
- [ ] Keep decoder-only and non-canonical forms in the independent decode
      suite.

### Safety and negative tests

The buffer refactor needs tests that distinguish a correct error from silent
truncation, out-of-bounds access, or partial writes.

Checklist:

- [ ] Add malformed descriptor, truncated parameter, invalid distance, invalid
      length, missing terminator, and impossible-size cases as applicable.
- [ ] Surround fixed output buffers with canaries and verify they remain
      unchanged.
- [ ] Verify insufficient fixed buffers fail before the first output write.
- [ ] Verify exact-size fixed buffers succeed without touching adjacent bytes.
- [ ] Verify borrowed-buffer alignment failures report the requested and actual
      alignment.
- [ ] Verify owned and mapped destinations resize to the exact prepared size.
- [ ] Verify failed prepare operations do not mutate the destination.
- [ ] Run suitable jobs under AddressSanitizer and UndefinedBehaviorSanitizer.

## Assembly Decoder Tests

### Oracle qualification and bootstrap

The shipped assembly decoders do not all have the same provenance or maturity.
Some are highly optimized, hand-crafted routines written by mdcomp's author.
Some of those have years of field use without known bug reports; others have
not been used externally. Field experience is useful evidence, but absence of
reports is not proof, and a hand-crafted assembly decoder may share format
assumptions with the C++ implementation even when it shares no code.

Record these dimensions independently for every routine:

- **provenance:** recovered/original, reference-derived, independently
  hand-crafted, or sharing authorship/design lineage with the C++ codec;
- **deployment:** no known use, limited/unknown use, or sustained use in named
  projects and versions;
- **verification:** unverified, independently sourced known-answer verified,
  cross-implementation verified, emulator verified, and real-hardware
  verified;
- **configuration:** assembler options, lookup-table mode, loop-unroll level,
  alignment mode, and any other materially distinct build variant tested.

Do not collapse these into a single confidence score. Preserve the evidence so
a test or fixture can state exactly what corroborates it.

The maintainer's initial assessment, to be replaced with per-file test
evidence, is:

| Routine group | Initial confidence | Required treatment |
| --- | --- | --- |
| MC68000 Comper and ComperX | Almost certainly correct | Run the complete suite and record optimization and field-use evidence |
| MC68000 Kosinski and Kosinski Plus | Almost certainly correct | Run direct and moduled suites and compare reference-accurate Kosinski streams |
| MC68000 Saxman | Almost certainly correct | Cross-check the recovered original encoder and boundary/trailing-data cases |
| Enigma and Nemesis | Correct; minor edits of original routines | Diff the edits from their originals and test both unchanged and modified behavior |
| Common moduled framework | Known working | Validate its queue, interrupt/resume, final-module, and DMA contracts in the hardware shim |
| All remaining routines | More suspicious | Keep provisional until the full bootstrap suite passes |

The current repository inventory does not contain a Nemesis assembly source.
Locate it, add it within the intended scope, or record it as an out-of-tree
reference before its confidence can contribute to this suite.

Before an assembly routine is used as the sole differential oracle for current
C++ encoder output, bootstrap it with compressed vectors not generated by that
current encoder. Use hand-authored token streams, published data, pinned
reference encoders, recovered original encoders, or the other architecture
where genuinely independent. A shared-lineage KENSSharp vector and a
same-author assembly decoder still provide valuable triangulation, but neither
alone proves the inherited interpretation.

For a hand-crafted routine with little or no field use:

1. test one hand-authored vector for every decoder token form;
2. test minimum and maximum lengths, distances, counts, headers, and
   terminators;
3. exercise descriptor reloads, overlap, wraparound, alignment, and read-ahead
   boundaries;
4. compare against every applicable pinned reference encoder or independent
   decoder;
5. run guard-region, instruction-budget, and malformed-input cases in the
   emulator;
6. run representative cases on real hardware when practical; and
7. only then mark the routine qualified for encoder differential testing.

Highly optimized routines require tests around the invariants introduced by
their optimizations, including loop-unroll tails, lookup-table and non-table
paths, source alignment, deliberate read-ahead, stack-based exits, and
interrupt/resume points where applicable.

Checklist:

- [ ] Add provenance, authorship/design lineage, deployment, verification, and
      configuration metadata for every assembly routine.
- [ ] Identify hand-crafted routines and record which have sustained field use
      and which have little or none.
- [ ] Record named downstream uses and known bug reports where available.
- [ ] Replace the maintainer's grouped assessment with per-file evidence and
      resolve the absent Nemesis assembly source.
- [ ] Bootstrap every provisional routine with non-current-encoder vectors
      before treating it as a differential oracle.
- [ ] Add optimization-boundary cases for every enabled assembly
      configuration.
- [ ] Keep field-use evidence distinct from independently verified test
      evidence in reports and fixture manifests.

### Toolchain submodule

Add [flamewing/asl-releases](https://github.com/flamewing/asl-releases) as a
pinned git submodule. It is the maintained ASL variant intended for these
sources, supports both target architectures, uses CMake, and is licensed under
GPL-2.0. Treat it as a separately executed build tool, not as a library linked
into mdcomp.

The assembly sources are decoders, so the test build assembles decoder
harnesses into binaries. A test then appends or loads the C++-generated
compressed payload at the harness's exported payload location.

Checklist:

- [ ] Add `asl-releases` below a documented third-party/submodule directory.
- [ ] Pin an exact reviewed commit rather than following the default branch.
- [ ] Build the required ASL host tools without installing them globally.
- [ ] Determine the minimal ASL targets needed, including binary conversion
      such as `p2bin` if required.
- [ ] Keep generated object, listing, symbol, and binary files in the build
      directory.
- [ ] Audit source-specific include paths and stage a build-tree include layout
      where a decoder expects paths such as `_inc/`, without rewriting the
      distributed source for each test.
- [ ] Make missing/uninitialized submodules produce an actionable CMake error.
- [ ] Document `git submodule update --init --recursive`.
- [ ] Record GPL-2.0 tool provenance without changing the license of mdcomp's
      own 0BSD assembly sources or their generated test payloads.

### MC68000 emulator

Use [kstenerud/Musashi](https://github.com/kstenerud/Musashi) as the initial
MC68000 candidate. It is a mature portable C emulator with explicit 8-, 16-,
and 32-bit memory callbacks and an execution API, and its permissive license
is compatible with a test harness.

Before pinning it, complete a small integration spike that builds only the
MC68000 core, supplies a project-owned `m68kconf.h`, executes a trivial ASL
program, and returns through the proposed sentinel mechanism.

Checklist:

- [ ] Add Musashi as a pinned git submodule after the integration spike passes.
- [ ] Generate or build Musashi's opcode source as a CMake dependency.
- [ ] Configure only the MC68000 features required by the decoder sources.
- [ ] Implement big-endian, bounds-checked memory callbacks.
- [ ] Support byte, word, and long accesses with explicit alignment checks.
- [ ] Expose register initialization and inspection to the harness.
- [ ] Stop on a sentinel return address, invalid access, exception, or
      instruction/cycle budget.
- [ ] Report PC, registers, and recent memory accesses on failure.
- [ ] Validate the emulator harness with a tiny hand-written copy routine before
      running a codec decoder.

### Z80 emulator

Use [superzazu/z80](https://github.com/superzazu/z80) as the initial Z80
candidate. It is a small C99 core under the MIT license, exposes memory and
port callbacks plus single-instruction stepping, and reports passing both
`zexdoc` and `zexall`. Its small API fits a deterministic 64 KiB decoder test
machine without full-system emulation.

Before pinning it, complete an integration spike that builds the core with the
project's warning settings, executes a trivial ASL Z80 program, and validates
the sentinel and instruction-budget mechanisms.

Checklist:

- [ ] Add `superzazu/z80` as a pinned git submodule after the integration spike
      passes.
- [ ] Wrap its C99 source in a warning-isolated CMake target.
- [ ] Implement a bounds-checked 64 KiB memory image.
- [ ] Provide deterministic memory and inert port callbacks.
- [ ] Expose register initialization and inspection to the harness.
- [ ] Stop on a sentinel return address, invalid access, halt, or
      instruction/cycle budget.
- [ ] Report PC, registers, and recent memory accesses on failure.
- [ ] Validate the emulator harness with a tiny hand-written copy routine before
      running a codec decoder.

### Architecture-neutral decoder harness

Define one logical harness API over both emulator backends:

```cpp
struct asm_decoder_case {
    architecture cpu;
    binary_image decoder;
    address entry;
    address payload;
    register_setup registers;
    byte_span compressed;
    mutable_byte_span output;
    execution_budget budget;
};
```

Each assembly wrapper should:

1. select the correct processor and origin for ASL;
2. include the unmodified decoder source;
3. expose stable entry, payload, and completion symbols;
4. arrange a return to a sentinel address;
5. reserve or identify non-overlapping stack and output regions.

The runtime harness loads the assembled decoder image, attaches the compressed
payload at the exported payload location, initializes the decoder's documented
source/destination registers, executes it, and compares the exact output bytes.
Keeping payload attachment at runtime avoids reassembling the decoder for every
generated round-trip case.

Checklist:

- [ ] Define architecture-neutral image, register, memory, result, and
      diagnostic types.
- [ ] Generate per-decoder ASL wrappers without modifying the distributed
      assembly sources.
- [ ] Export or parse entry, payload, completion, and other required symbol
      addresses.
- [ ] Append or load each compressed payload at the declared payload address.
- [ ] Poison output memory and add guard regions before execution.
- [ ] Initialize every register on which a decoder's calling convention relies.
- [ ] Detect reads beyond the attached compressed payload where the format
      promises an exact bound.
- [ ] Detect writes outside the declared output region.
- [ ] Enforce deterministic instruction or cycle budgets.
- [ ] Compare exact output length and bytes.
- [ ] Produce a compact failure dump containing the fixture, architecture,
      decoder, PC, registers, and access violation.

### Assembly test modes

For every assembly decoder:

1. **C++ encode plus assembly decode**

   ```text
   asm_decode(cpp_encode(uncompressed)) == uncompressed
   ```

2. **Known-answer assembly decode**

   ```text
   asm_decode(known_compressed) == known_uncompressed
   ```

The second mode must include vectors not generated by the current C++ encoder.
Where both MC68000 and Z80 decoders exist, run the same compatible compressed
fixture through both.

Passing mode 1 proves interoperability between the current encoder and
assembly decoder, but they can still share a format mistake. Passing mode 2
qualifies the assembly routine only to the strength of the known-answer
vector's provenance. Report both results together with the routine's
deployment and verification metadata.

Initial direct-decoder inventory:

| Codec | MC68000 | Z80 |
| --- | --- | --- |
| Comper | `Comper.asm` | — |
| ComperX | `ComperX.asm` | — |
| Enigma | `Enigma.asm` | — |
| Nemesis | Not present in the current tree; locate or document as out of tree | — |
| Kosinski | `Kosinski.asm` | `Kosinski.a80` |
| Kosinski Plus | `KosinskiPlus.asm` | `KosinskiPlus.a80` |
| Rocket | `Rocket.asm` | — |
| Saxman | `Saxman.asm` | — |
| SnKRLE | `SNKRLE.asm` | — |

The MC68000 moduled queue sources access Mega Drive hardware state and are more
than pure decoder routines. Test direct decoders first, then add a minimal
hardware shim for the queue variants rather than pretending a CPU-only harness
is sufficient.

Checklist:

- [ ] Classify every direct decoder's provenance, authorship/design lineage,
      field-use history, and existing validation evidence.
- [ ] Locate the original-derived Nemesis routine or document why it remains
      outside the shipped assembly inventory.
- [ ] Bootstrap decoders with little or no field use before using their results
      to audit C++ encoders.
- [ ] Add C++ encode plus assembly decode tests for every direct MC68000
      decoder.
- [ ] Add independent known-answer tests for every direct MC68000 decoder.
- [ ] Add both test modes for the Kosinski Z80 decoder.
- [ ] Add both test modes for the Kosinski Plus Z80 decoder.
- [ ] Run shared Kosinski fixtures through both architectures.
- [ ] Inventory the memory-mapped hardware used by the moduled queue sources.
- [ ] Implement only the minimal Mega Drive hardware shim needed by those
      sources.
- [ ] Add moduled Comper, ComperX, Kosinski, and Kosinski Plus assembly tests.

## Fixtures and Coverage Manifests

Keep small redistributable fixtures in the repository. Keep generated,
large-corpus, and failure-reproduction data in the build tree.

Suggested layout:

```text
tests/
  asm/
    m68000/
    z80/
    wrappers/
  codecs/
  fixtures/
    <codec>/
      README.md
      *.compressed
      *.raw
  support/
```

Each codec's fixture descriptor should record:

- fixture name and format variant;
- compressed and uncompressed file paths;
- source and license;
- generator tool, version, command, options, and hashes where applicable;
- whether the compressed form is independent of mdcomp, shares mdcomp lineage,
  or was produced by the current implementation;
- whether it is independently specified, hand-authored, reference-generated,
  or a current-branch regression artifact;
- which assembly decoders consume it and each routine's verification
  classification;
- encoder edge types observed;
- decoder edge types present;
- boundary obligations covered;
- applicable C++, MC68000, Z80, and moduled test modes.

Checklist:

- [ ] Choose a manifest representation that requires no heavyweight runtime
      parser.
- [ ] Add a provenance README for every codec fixture directory.
- [ ] Reject fixtures whose redistribution terms are unknown.
- [ ] Prefer synthetic, hand-authored, or published examples over ROM-derived
      data.
- [ ] Preserve the deterministic source generator or raw input for every
      reference-generated compressed fixture.
- [ ] Validate fixture checksums so accidental edits are obvious.
- [ ] Add a coverage-summary test that reports missing edge obligations by
      codec.
- [ ] Keep fixtures small enough for normal source control and fast CI.

## TDD Workflow for New Codecs

Before production code for a new codec is accepted:

1. write a normative token/edge manifest;
2. add independent decoder vectors for every understood token form;
3. add a failing decoder test;
4. implement the smallest decoder change that passes;
5. add encoder round-trip inputs and expected edge obligations;
6. add a failing encoder test;
7. implement encoding until the round-trip and edge obligations pass;
8. add assembly differential tests when a compatible assembly decoder exists;
9. add malformed-input and exact-buffer tests through the managed-buffer API.

Checklist:

- [ ] Add this workflow to the contributor documentation.
- [ ] Make the codec implementation plan link to the required fixture and edge
      manifest gates.
- [ ] Provide a copyable test adapter and fixture skeleton.
- [ ] Require new format variants to add their own manifest obligations.
- [ ] Require every fixed bug to add a regression fixture or generated case.

## CI and Reproducibility

Checklist:

- [ ] Run fast C++ tests on every supported compiler/platform CI job.
- [ ] Run assembly tests on at least Linux initially, then enable Windows and
      macOS after the submodule toolchains are verified there.
- [ ] Add a sanitizer job for C++ codec and emulator harness code.
- [ ] Add an optional compiler line/branch coverage job.
- [ ] Run slow randomized and exhaustive tests on a scheduled or explicitly
      requested job.
- [ ] Cache submodule builds without allowing unpinned network downloads.
- [ ] Print dependency commit IDs in assembly-test diagnostics.
- [ ] Make all pseudo-random tests deterministic and reproducible by seed.
- [ ] Prevent emulator tests from hanging through both CTest timeouts and
      internal execution budgets.
- [ ] Document how to run one codec, one architecture, or one fixture locally.

## Milestone Checklist

### 0. Framework skeleton

- [ ] Add CTest and Catch2.
- [ ] Create the common C++ codec test adapter.
- [ ] Add one round-trip and one known-answer smoke test.
- [ ] Establish fixture provenance conventions.
- [ ] Establish reference-tool pinning and shared-lineage classifications.

### 1. Existing C++ regression baseline

- [ ] Add round-trip tests for every working basic codec.
- [ ] Add round-trip tests for every working moduled codec.
- [ ] Add at least one independent known-answer vector per decoder.
- [ ] Generate the synthetic reference-encoder corpus and audit every
      current-branch encoder and option.
- [ ] Complete the format-edge manifests.
- [ ] Reach 100% declared encoder and decoder edge coverage.

### 1a. Buffer-migration constexpr checks

- [ ] Add the fixed-storage LZSS adaptor inverse harness once checked memory
      cursors are available.
- [ ] Make all encoder-supported adaptor edge forms pass their compile-time
      semantic inverse and `output_edge` equivalence assertions.
- [ ] Run the broader runtime form, parameter, and position matrix through
      CTest.

### 2. Assembly toolchain and emulator spikes

- [ ] Integrate and pin `asl-releases`.
- [ ] Validate Musashi and pin its submodule commit.
- [ ] Validate `superzazu/z80` and pin its submodule commit.
- [ ] Assemble and execute one trivial program on each emulator.

### 3. Direct assembly decoders

- [ ] Add reusable MC68000 and Z80 harnesses.
- [ ] Complete the provenance, deployment, verification, and configuration
      inventory.
- [ ] Bootstrap every provisional or previously unused routine.
- [ ] Add C++-encode/assembly-decode tests.
- [ ] Add independent known-answer assembly tests.
- [ ] Cover every direct decoder in the inventory.

### 4. Moduled assembly decoders

- [ ] Document the required Mega Drive hardware interactions.
- [ ] Add the minimal deterministic hardware shim.
- [ ] Test every shipped moduled assembly decoder.

### 5. Blocking gate

- [ ] Run the full baseline suite on the current pre-refactor code.
- [ ] Complete the reference-encoder audit of the current branch.
- [ ] Record any known failures explicitly; do not silently bless them.
- [ ] Enable the suite in CI.
- [ ] Mark the buffer refactor unblocked.
- [ ] Mark codec implementation unblocked once its buffer prerequisites also
      land.

## Completion Criteria

This prerequisite plan is complete when:

- every working existing C++ codec has round-trip and independent known-answer
  coverage;
- every current encoder has been audited against deterministic synthetic
  reference fixtures and applicable assembly decoders;
- every declared encoder and decoder edge type is exercised;
- fixed-buffer and malformed-input safety tests needed by the refactor exist;
- every shipped direct MC68000 and Z80 decoder has recorded provenance and
  validation evidence and runs under an emulator in both required test modes;
- every previously unused or provisional assembly routine is bootstrapped with
  vectors not generated by the current C++ encoder before it is used as an
  oracle;
- moduled assembly decoders either pass under the minimal hardware shim or
  have a separately approved, explicit deferral;
- the suite runs through CTest in CI with deterministic inputs and bounded
  execution;
- fixture and third-party dependency provenance is documented.

## Deferred Decisions

- Whether Catch2 remains the long-term framework after the initial spike.
- The exact submodule directory names.
- Whether edge observers are compiled under a test-only definition or provided
  by standalone compressed-stream inspectors.
- The fixture manifest representation.
- Whether compiler line/branch coverage has a required threshold in addition
  to mandatory format-edge coverage.
- Which CI platforms run assembly tests on every commit versus scheduled runs.
