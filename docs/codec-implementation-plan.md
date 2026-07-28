# Codec Implementation Plan

Status: implementation blocked by the testing framework and buffer refactor;
research and planning may proceed.

> [!IMPORTANT]
> First establish the regression and TDD infrastructure in
> [`testing-framework-plan.md`](testing-framework-plan.md), then land the
> foundational changes in [`buffer-refactor-plan.md`](buffer-refactor-plan.md).
> Building the codecs on checked memory cursors, managed buffers, and
> prepare/commit plans avoids duplicating temporary stream-based interfaces
> that would immediately need another refactor.

This document tracks codec implementation work that is separate from the
general buffer work in
[`buffer-refactor-plan.md`](buffer-refactor-plan.md).
Normative format notes and mdcomp implementation/optimality notes are shared
deliverables with [`documentation-plan.md`](documentation-plan.md).

Here, "implementation" includes both adding codecs and improving existing
codecs. The formats span LZ-derived, RLE, entropy-coded, and graphics-specific
schemes. Implementations should share graph, memory-cursor, and multi-stream
infrastructure where their structures overlap rather than being divided by
codec family.

## Goals

1. Implement the requested C42, Gunstar Heroes, Streets of Rage 2, SLZ, UFTC,
   Batman & Robin, Compile/Puyo Puyo, Kid Chameleon, and Space Harrier codecs.
2. Give each codec in scope a checked, memory-only decoder and encoder where
   the format is sufficiently understood.
3. Improve the shared LZSS infrastructure to support formats with separate
   descriptor and parameter/data streams.
4. Refactor Enigma and SnKRLE to produce reusable parse plans and exact output
   sizes.
5. Replace Nemesis's greedy and heuristic decisions with a formally modelled
   optimizer, or document precisely which part prevents a practical proof of
   global optimality.
6. Add authoritative format notes, reference vectors, differential tests, and
   small-input optimality oracles.
7. Integrate the codecs with the managed-buffer and eventual C APIs without
   duplicating boundary I/O code.

## Scope and Dependencies

Codec implementation in this plan is blocked first on the testing framework's
fixture, contract-test, and format-edge coverage infrastructure, and then on
the following work from the buffer refactor:

- checked input/output memory cursors and bit cursors;
- `codec_result` and structured error reporting;
- managed-buffer size/alignment negotiation;
- reusable prepare/commit plans;
- fixed-buffer preflight;
- legacy stream and file adapters.

LZ-derived codec work additionally depends on:

- a reusable shortest-path parsing core;
- reusable LZ match enumeration;
- support for split descriptor/data streams from
  [issue #25](https://github.com/flamewing/mdcomp/issues/25) where noted.

Each codec workstream must follow the TDD gates in
[`testing-framework-plan.md`](testing-framework-plan.md): define its
format-edge manifest, add independent decoding vectors, and add failing
decoder and encoder tests before completing the implementation.

The first implementation target is portable C++ code. Original game assembly
or third-party implementations are references and differential-test oracles,
not code to copy without a compatible license.

## Source Inventory

| Codec | Primary references | Initial classification | Readiness |
| --- | --- | --- | --- |
| C42 art | [SH-2 assembly](Decompress_C42Art-ASM.txt), [C++ reference](Decompress_C42Art-C++.txt), [pseudocode](Decompress_C42Art-Pseudocode.txt) | Bit-packed structured graphics stream plus unpacked representation | Decoder specification available; encoder rules need formalization |
| Gunstar Heroes | [Issue #7](https://github.com/flamewing/mdcomp/issues/7), [SpritesMind analysis](https://gendev.spritesmind.net/forum/viewtopic.php?t=2921) | Stateful bitstream/RLE-like graphics transform | Decoder reference available; encoder and termination semantics need research |
| Streets of Rage 2 | [Issue #18](https://github.com/flamewing/mdcomp/issues/18), [reference tools](https://github.com/Clownacy/sor2-comp) | RLE-family codec | Accurate compressor and decoder references available |
| SLZ | [Issue #19](https://github.com/flamewing/mdcomp/issues/19), [format description](https://www.plutiedev.com/format-slz) | Conventional LZ token stream; 16- and 24-bit size variants | Format and tools available |
| UFTC | [Issue #20](https://github.com/flamewing/mdcomp/issues/20), [format description](https://www.plutiedev.com/format-uftc) | Tile/block dictionary transform; UFTC15 and UFTC16 variants | Format and tools available |
| Batman & Robin | [Issue #22](https://github.com/flamewing/mdcomp/issues/22) | LZSS with split descriptor and inline-data streams | Blocked on multi-stream infrastructure |
| Compile/Puyo Puyo | [Issue #23](https://github.com/flamewing/mdcomp/issues/23) | LZ stream with literal runs and a 256-byte window | Format and decoder assembly available |
| Kid Chameleon | [Issue #24](https://github.com/flamewing/mdcomp/issues/24), [multi-stream dependency](https://github.com/flamewing/mdcomp/issues/25) | LZSS with split streams | Blocked on multi-stream infrastructure |
| Space Harrier II | [Issue #27](https://github.com/flamewing/mdcomp/issues/27) | Per-tile repeated-byte masks plus residual literals | Decoder assembly available; encoder appears locally optimizable |

The Gunstar format is also reported in Alien Soldier, which may provide a
second source of validation fixtures. Batman & Robin may share its format
lineage with other Zyrinx titles; this is useful research context but is not a
requirement for the first implementation.

## Common Codec Architecture

### Reusable token plans

Introduce a generic weighted parse-plan layer for codecs that are not naturally
expressed by the existing LZSS adaptor:

```cpp
struct token_edge {
    size_t source_begin;
    size_t source_end;
    size_t bit_cost;
    token_kind kind;
    token_payload payload;
};

struct token_plan {
    size_t input_size;
    size_t output_bits;
    std::vector<token_edge> path;
};
```

The concrete representation may be templated by codec, but it should preserve
these properties:

- preparation retains only the selected path or other compact encoding state;
- commit does not redo the parse search;
- edge costs include descriptors and inline parameters;
- header, terminator, and final bit/byte rounding are included in total size;
- tie-breaking is deterministic;
- stateful formats may use graph nodes richer than a plain input position;
- the plan reports exact output size before destination allocation.

LZ-derived implementations should reuse LZ match discovery while supplying
their own token types and costs. Non-LZ formats may reuse only the
shortest-path and plan machinery.

### Definition of optimality

For this plan, "perfect" or "optimal" compression means the shortest valid
encoded byte sequence for the given input, format variant, and public options,
including:

- headers and code tables;
- token and descriptor bits;
- terminators;
- mandatory format padding and final bit rounding.

If byte size is equal, deterministic tie-breaking chooses one canonical output.
Compatibility modes that intentionally reproduce an original game's encoder
may use different tie-breaking and are tested separately.

### Format documentation

Before implementing an encoder, add the codec's normative format note from
[`documentation-plan.md`](documentation-plan.md), including:

- byte order and bit order;
- headers and representable size ranges;
- token grammar and termination;
- decoder state and history initialization;
- overlap and wraparound behaviour;
- required input/output alignment;
- optional parameters and variants;
- malformed-input rules;
- whether trailing input is permitted;
- licensing and provenance of references.

Where encoder choices are not dictated by the format, add or update the
corresponding mdcomp implementation note with the chosen algorithm, objective
and cost model, tie-breaking, complexity, alternatives, and evidence for any
optimality or compatibility claim. The format note and test edge manifest must
use a reconcilable token inventory.

## Codec Implementation Workstreams

### C42 art

The existing `artc42` target is a stub and should be completed rather than
creating a duplicate codec.

The supplied documents describe two input forms:

- bit-packed data, identified by byte `0x42` after a decompressed-size word;
- an unpacked row representation that is also the decoder's normalized output.

The decoder also accepts a palette offset. Nonzero palette entries have that
offset applied, while transparent zero remains zero.

Checklist:

- [ ] Reconcile the assembly, C++ sample, and pseudocode into one normative
      format note.
- [ ] Confirm the endianness of every coordinate and size field.
- [ ] Confirm whether row end-X is inclusive or exclusive and resolve the
      apparent loop-count ambiguity in the documents.
- [ ] Define the normalized decoded representation, including its terminating
      row and returned logical size.
- [ ] Define palette-offset behaviour and its public option type.
- [ ] Implement checked detection of packed versus unpacked input.
- [ ] Implement the MSB-first packed bitstream decoder.
- [ ] Implement unpacked normalization and validation.
- [ ] Reject zero/oversized bit widths, backwards rows, size mismatches, and
      truncated terminators.
- [ ] Derive encoder base values and bit widths from normalized input.
- [ ] Decide whether encoding always emits packed C42 or also supports an
      unpacked/pass-through variant.
- [ ] Implement exact encoded-size calculation and reusable encode plans.
- [ ] Add hand-built one-row, transparent-pixel, palette-offset, empty, and
      malformed vectors.
- [ ] Replace the stub's `false` results and add boundary/C API coverage.

### Gunstar Heroes

The reference decoder is a resumable state machine over an MSB-first bitstream.
It expands sparse values into a 128-byte intermediate buffer and then compacts
that state into output. The file begins with a decompression-step count.

Checklist:

- [ ] Transcribe the reference algorithm into a format/state-machine note
      without depending on unlicensed source code.
- [ ] Determine the exact relationship between step count, intermediate bytes,
      and final output bytes.
- [ ] Document the saved bit state required to resume mid-word.
- [ ] Document the sparse-write markers, smear/repetition stage, compaction,
      and termination conditions.
- [ ] Obtain redistributable fixtures or construct fixtures from a clean-room
      encoder model.
- [ ] Differentially validate the decoder against the published C# algorithm.
- [ ] Compare Gunstar Heroes and Alien Soldier streams for format identity.
- [ ] Implement a checked, resumable memory decoder.
- [ ] Model inverse token choices and determine whether encoding is a local,
      dynamic-programming, or more general state-search problem.
- [ ] Build a short-input exhaustive encoder oracle.
- [ ] Implement a deterministic encoder only after the inverse model is
      validated.
- [ ] Document whether random-access/resumable decoding is public API surface
      or an internal capability.

### Streets of Rage 2

The reference repository contains a decoder, a normal compressor, and an
accurate compressor intended to reproduce original ROM output.

Checklist:

- [ ] Record the token grammar, header fields, terminator, and state.
- [ ] Audit the reference repository's zlib license and record attribution.
- [ ] Generate synthetic differential vectors without committing ROM data.
- [ ] Port the decoder to checked memory cursors.
- [ ] Implement an `accurate` compatibility mode with reference tie-breaking.
- [ ] Determine whether a separate shortest-output mode can beat the accurate
      compressor.
- [ ] If choices interact, express them as a token graph and verify with a
      small exhaustive oracle.
- [ ] Test byte-identical output against the accurate reference tool.
- [ ] Add malformed-stream and boundary tests.

### SLZ and SLZ24

SLZ uses a big-endian uncompressed length followed by groups of eight tokens.
A descriptor zero bit is a literal; a one bit is a two-byte match with a
12-bit distance and four-bit length. SLZ24 uses a three-byte size header.

Checklist:

- [ ] Write the normative SLZ/SLZ24 format note from the published
      specification and reference source.
- [ ] Confirm distance and length biases at all extrema.
- [ ] Implement header-derived destination sizing and checked decoding.
- [ ] Implement match enumeration and shortest-path parsing.
- [ ] Include descriptor-group boundaries and final partial descriptor cost in
      graph weights.
- [ ] Add explicit SLZ16 and SLZ24 options and size-range validation.
- [ ] Differentially test both variants against the published tools.
- [ ] Test overlapping matches, maximum distance, maximum length, and output
      ending in every descriptor-bit position.

### UFTC15 and UFTC16

UFTC splits every 8x8 4bpp tile into four 4x4 blocks, stores the unique block
dictionary, and represents each tile with four block offsets. UFTC16 supports
up to 8192 blocks; UFTC15 uses the older signed-offset range.

Checklist:

- [ ] Write the normative tile, block, dictionary, and index layout.
- [ ] Confirm byte order and the exact UFTC15 signed-offset restriction.
- [ ] Define how the decoder obtains tile count from the bounded input view.
- [ ] Validate dictionary size, index alignment, index bounds, and complete
      encoded tiles.
- [ ] Implement random-access-capable tile decoding.
- [ ] Implement deterministic unique-block collection.
- [ ] Preserve first-occurrence ordering unless another canonical order gives a
      compatibility benefit.
- [ ] Emit UFTC16 by default and provide an explicit UFTC15 compatibility
      option.
- [ ] Reject inputs exceeding the selected dictionary range.
- [ ] Differentially test against published UFTC tools.
- [ ] Test duplicate-heavy, all-unique, empty, maximum-dictionary, and malformed
      inputs.

### Batman & Robin

The format uses a `0x6a0`-byte sliding window and match lengths up to `0x113`.
Descriptor fields and inline dictionary parameters occupy separate streams,
with a 32-bit relative offset to the parameter stream.

Checklist:

- [ ] Keep this codec blocked until generic multi-stream LZ input/output is
      available.
- [ ] Recover or replace the expired/reference decompressor linked from the
      issue.
- [ ] Write a normative token and split-stream layout.
- [ ] Confirm bit order, offset base, termination encoding, and all length and
      displacement boundaries.
- [ ] Decide whether shared/global parameter streams are supported or rejected
      by the first API.
- [ ] Extend LZ match enumeration for the `0x6a0` window and `0x113` maximum
      length.
- [ ] Implement graph costs across descriptor and parameter streams.
- [ ] Determine whether descriptor/data overlap is legal and should be
      optimized.
- [ ] Implement decoder and encoder after multi-stream support lands.
- [ ] Add synthetic split-stream, maximum-match, and terminator vectors.

### Compile/Puyo Puyo

The stream has three token forms:

- zero descriptor: end;
- `0xxxxxxx`: copy 1-127 following literal bytes;
- `1xxxxxxx oooooooo`: copy 3-130 bytes from distance 1-256.

Checklist:

- [ ] Verify the issue's disassembly, especially the apparent copied loop-label
      typo in the match path.
- [ ] Document the initial 256-byte history state and byte-wrap semantics.
- [ ] Implement checked decoding, including overlapping and wrapping matches.
- [ ] Model literal runs and matches as graph edges.
- [ ] Include the one-byte terminator in total graph cost.
- [ ] Implement exact shortest-path encoding with deterministic tie-breaking.
- [ ] Test literal lengths 1 and 127, match lengths 3 and 130, distances 1 and
      256, history wrap, and empty input.

### Kid Chameleon

Kid Chameleon is an LZSS format with descriptors stored separately from inline
data. Existing references include a game disassembly and clownlzss
implementation.

Checklist:

- [ ] Keep this codec blocked until issue #25's multi-stream support exists.
- [ ] Audit and summarize the disassembly and clownlzss references.
- [ ] Write the normative token, descriptor, and data-stream layout.
- [ ] Confirm history initialization, match boundaries, terminator, and stream
      offset encoding.
- [ ] Add its stream policy to the generic multi-stream LZ layer.
- [ ] Implement graph-based encoding and checked decoding.
- [ ] Differentially test against both available reference implementations.

### Space Harrier II

Every decoded tile is 32 bytes. An encoded tile starts with the number of
repeated-byte entries. Each entry contains one byte and a 32-bit placement mask;
remaining positions are filled by following literal bytes. A negative count
byte terminates the stream.

For a fixed tile, selecting a repeated-byte entry costs five bytes and removes
one literal for every covered occurrence. This suggests a local optimum:
select each byte value whose mask saves more than its five-byte entry cost,
subject to confirmed count and format constraints.

Checklist:

- [ ] Write the normative tile-entry, mask-bit-order, literal, and terminator
      layout.
- [ ] Confirm whether a zero placement mask is legal.
- [ ] Confirm the maximum entry count and exact negative terminator value.
- [ ] Implement checked tile-by-tile decoding and output-size tracking.
- [ ] Prove the per-tile entry-selection rule or replace it with exhaustive
      subset testing over the at most 32 distinct values.
- [ ] Define deterministic behaviour for equal-cost entries.
- [ ] Implement the exact per-tile encoder.
- [ ] Test no repeated values, one repeated value, all-identical tiles,
      equal-cost frequencies, all mask bit positions, and truncated tiles.

## Existing Codec Improvements

### Multi-stream LZSS support

Add reusable support for LZSS formats that store descriptor bits separately
from literal and match parameters, as tracked by
[issue #25](https://github.com/flamewing/mdcomp/issues/25). This is both an
improvement to the shared LZSS implementation and a prerequisite for the Kid
Chameleon and Batman & Robin codecs.

Most of the required encoding structure already exists. The optimal parser
separately tallies descriptor size and total encoded size and returns them as
`parse_result::desc_size` and `parse_result::file_size` (both currently counted
in bits). The current
`Adaptor::ostream_t` also supports the two mixed-stream variants through early
or late descriptor reload. Multi-stream encoding should therefore be a layout
policy over the existing parse result, not a new parser.

The initial layout policies should be:

- a single mixed stream with early descriptor reload;
- a single mixed stream with late descriptor reload;
- all descriptors followed by parameter/data bytes;
- all parameter/data bytes followed by descriptors.

For encoding, the prepare phase can use the existing descriptor and total-size
accounting to locate both regions of a split stream. `Adaptor::ostream_t`
should then route descriptor writes and parameter/data writes to the appropriate
checked cursor while leaving `Adaptor::encode_edge` and the selected parse
unchanged. This is expected to require only a relatively small extension to
`Adaptor::ostream_t`, especially after it is converted to the buffer refactor's
memory-backed cursors.

Decoding should be simpler because it does not need the optimal parser.
`Adaptor::istream_t` should apply the layout policy when establishing its
descriptor and parameter/data cursors, then preserve the existing
`descriptor_bit`, `descriptor_bits`, `get_byte`, and bulk-read interface for
codec-specific decoding. Most decoder-side changes should consequently be
localized to `Adaptor::istream_t`.

Checklist:

- [ ] Document the existing `desc_size`, `file_size`, descriptor rounding,
      dummy-descriptor, and early/late reload invariants.
- [ ] Define an explicit stream-layout policy covering mixed early reload,
      mixed late reload, descriptors-then-data, and data-then-descriptors.
- [ ] Decide whether this policy replaces `need_early_descriptor` or subsumes
      it behind a compatibility definition.
- [ ] Confirm that the existing parse accounting is sufficient for all four
      layouts and adjust only any layout-dependent padding or header costs.
- [ ] Use the prepared descriptor and total sizes to derive and validate the
      descriptor and parameter/data output regions.
- [ ] Make the minor `Adaptor::ostream_t` changes needed to route descriptor
      and parameter/data writes through separate checked cursors.
- [ ] Preserve existing `Adaptor::encode_edge` implementations across all
      layouts where their token formats are otherwise unchanged.
- [ ] Update `Adaptor::istream_t` to initialize mixed or independent input
      cursors according to the selected layout policy.
- [ ] Support independent checked descriptor and parameter/data cursors during
      decoding.
- [ ] Preserve the existing adaptor-facing input operations so codec decoders
      do not need to know how their streams are physically arranged.
- [ ] Validate stream offsets, extents, truncation, and any format-specific
      overlap rules before unsafe access is possible.
- [ ] Retain the selected parse and exact per-stream sizes in a prepare plan.
- [ ] Negotiate the complete destination size with the managed buffer before
      committing any stream.
- [ ] Emit all streams from the retained plan without rerunning match discovery
      or parsing.
- [ ] Provide policies for Kid Chameleon and Batman & Robin without embedding
      game-specific rules in the shared layer.
- [ ] Add malformed-input, boundary, overlap, exact-size, and deterministic
      tie-breaking tests.
- [ ] Document how future split-stream LZSS codecs add a policy and fixtures.

### Enigma

The current encoder selects a common value and an initial incrementing value,
then greedily prioritizes incrementing runs, common-value runs, +1/-1
sequences, and buffered inline values.

For fixed header choices, literal runs and most sequence modes naturally form
weighted edges. Incrementing tokens also mutate the current incrementing value,
so globally optimal parsing may require graph states of
`(input position, current incrementing value)` rather than position alone.

Checklist:

- [ ] Document every token form and its exact bit cost.
- [ ] Include six-byte header cost, terminator, and final bit rounding.
- [ ] Separate header selection from token parsing in the implementation.
- [ ] Build a decoder-backed exhaustive oracle for short word sequences.
- [ ] Implement an optimal graph for fixed common and incrementing header
      values.
- [ ] Determine the reachable incrementing-value state space and bound it
      without losing optimal solutions.
- [ ] Derive a finite set of common-value and initial-increment candidates from
      the input.
- [ ] Compare candidate enumeration with a joint-state search.
- [ ] Prove candidate pruning or retain the exhaustive candidate set.
- [ ] Store the selected token path as an Enigma encode plan.
- [ ] Compare output against the current greedy encoder and the short-input
      oracle.

### SnKRLE

SnKRLE writes the uncompressed size, then uses repeated adjacent bytes as an
implicit RLE marker with an eight-bit additional count. The current encoder is
greedy and is likely optimal, but that should be demonstrated rather than
assumed.

Checklist:

- [ ] Write a precise grammar for singleton bytes, runs, the count-255 case,
      and continuation after a maximum run.
- [ ] Fix or define empty-input behaviour.
- [ ] Model valid encodings as a position graph.
- [ ] Compare the graph result with the current greedy parser for exhaustive
      short inputs and randomized longer inputs.
- [ ] Prove the greedy rule by an exchange argument if the results support it.
- [ ] Keep the graph implementation only if it improves correctness,
      maintainability, or tie-breaking; otherwise retain the proven greedy
      parser.
- [ ] In either case, emit a reusable plan with exact encoded size.
- [ ] Reject uncompressed sizes that do not fit the format header.

## Nemesis Algorithm Research

### Problem statement

Nemesis encoding currently combines:

- greedy parsing of same-nibble runs;
- heuristic symbol weights for a length-limited prefix code;
- decisions about which runs receive table codes and which use the 13-bit
  inline form;
- heuristics for splitting long runs;
- code-table serialization overhead;
- normal and progressive-XOR input transforms;
- four candidate encodes using two comparison strategies.

These decisions are coupled. Package-merge is exact for length-limited Huffman
coding only when symbol frequencies are already fixed. Here, symbol frequencies
depend on the parse, and the best parse depends on code lengths and inline-token
availability.

### Formal model

Model the format in three layers:

1. **Parse layer**
   - Nodes are nibble positions.
   - Edges cover legal same-nibble runs.
   - Each edge may use a table symbol or the inline escape.
2. **Code layer**
   - Select table symbols and code lengths up to the format limit.
   - Enforce the prefix/Kraft constraints and Nemesis table ordering.
3. **Container layer**
   - Include mode/header flags, serialized table size, escape costs, final bit
     rounding, and normal versus XOR preprocessing.

The objective is total stored bits, not merely weighted payload-code length.

### Research and oracle checklist

- [ ] Write a normative Nemesis grammar from the decoder before changing the
      encoder.
- [ ] Inventory every current heuristic and construct a fixture that exercises
      it.
- [ ] Separate parsing, symbol-frequency collection, code construction, table
      serialization, and bit emission into independently testable components.
- [ ] Implement optimal parsing for a fixed codebook using a shortest-path DAG.
- [ ] Replace the current coin implementation with, or validate it against, a
      known exact length-limited Huffman/package-merge implementation for fixed
      frequencies.
- [ ] Include table-entry overhead and inline availability when deciding
      whether a symbol belongs in the codebook.
- [ ] Build an exhaustive solver for very short nibble streams.
- [ ] Investigate a mixed-integer, constraint-programming, or branch-and-bound
      oracle for somewhat larger samples.
- [ ] Use the oracle to measure the optimality gap of the current four
      candidates and of proposed algorithms.
- [ ] Investigate alternating fixed-codebook parse and fixed-parse code
      optimization as a practical upper-bound heuristic, without describing it
      as globally optimal.
- [ ] Determine whether the small Nemesis alphabet and maximum code length make
      exact codebook subset/length enumeration practical.
- [ ] Analyse whether long-run splitting can be integrated directly into parse
      edges, eliminating its post-processing heuristic.
- [ ] Optimise normal and progressive-XOR modes independently and compare their
      complete stored sizes.
- [ ] Record a proof, complexity bound, and oracle comparison before claiming
      perfect compression.

### Literature review

Start with:

- Larmore and Hirschberg's package-merge work on optimal length-limited Huffman
  codes: [A Fast Algorithm for Optimal Length-Limited Huffman Codes](https://www.ics.uci.edu/~dan/pubs/LenLimHuff.pdf).
- Baer's generalization of bounded-length Huffman coding:
  [D-ary Bounded-Length Huffman Coding](https://arxiv.org/abs/cs/0701012).
- Ferragina, Nitto, and Venturini on separating dictionary match discovery from
  bit-optimal variable-length parsing:
  [Bit-Optimal Lempel-Ziv Compression](https://arxiv.org/abs/0802.0835).

Literature tasks:

- [ ] Search for joint tokenization and prefix-code optimization.
- [ ] Search for two-part/minimum-description-length coding with explicit
      dictionary or code-table cost.
- [ ] Search for optimal parsing with an escape symbol and optional phrase
      alphabet.
- [ ] Identify which results assume fixed frequencies or fixed tokenization and
      therefore do not solve the full Nemesis problem.
- [ ] Summarize applicable algorithms, proofs, and complexity in a dedicated
      Nemesis design note.

### Nemesis acceptance levels

Track progress honestly:

1. **Correct implementation:** byte-compatible decoder and encoder with
   isolated components.
2. **Strict improvement:** never worse than the current encoder on the corpus
   and better on at least one documented case.
3. **Oracle-verified:** globally optimal for all inputs within the exhaustive
   oracle's bound.
4. **Globally optimal:** supported by a proof or an exact solver with a
   practical bound covering supported inputs.

Do not block correctness and buffer integration on achieving level 4.

## Testing and Corpus

Every codec requires:

- decoder tests from hand-authored tokens covering every branch;
- encoder round trips;
- deterministic golden output;
- exact size and `input_consumed` assertions;
- zero, minimum, maximum, and one-past-maximum field tests;
- truncated headers, tokens, bitstreams, and dictionaries;
- overflow and invalid-reference tests;
- fixed-buffer and alignment tests from the buffer plan;
- differential tests where a licensed reference implementation exists;
- fuzzing with the decoder under ASan and UBSan.

ROM-derived data must not be committed unless redistribution is clearly legal.
Prefer synthetic vectors, published examples, or generated fixtures. Record
the source and license of every external fixture.

Optimal encoders additionally require:

- exhaustive comparison on small inputs;
- randomized comparison against the oracle where feasible;
- a regression corpus containing known greedy failures;
- deterministic tie-breaking tests;
- proof that reported size includes headers, tables, terminators, and rounding.

## Milestone Checklist

### 0. Research baseline

- [x] Inventory the three local C42 documents.
- [x] Review issues #7, #18, #19, #20, #22, #23, #24, and #27.
- [x] Record issue #25 as the Kid Chameleon and split-stream dependency.
- [ ] Complete source/license/provenance notes for every reference.
- [ ] Establish the synthetic and redistributable fixture corpus.
- [ ] Add common optimality-oracle test helpers.

### 1. Shared parsing support

- [ ] Extract a generic weighted shortest-path/token-plan utility.
- [ ] Support stateful graph nodes for formats such as Enigma.
- [ ] Make plan emission independent of stream I/O.
- [ ] Add deterministic equal-cost tie-breaking.
- [ ] Add a brute-force reference solver for small parse graphs.

### 2. First implementation group

- [ ] Implement C42 decoding and replace the `artc42` stub.
- [ ] Implement UFTC16 decoding/encoding and UFTC15 compatibility.
- [ ] Implement Space Harrier II decoding/encoding.
- [ ] Implement Streets of Rage 2 decoding and accurate encoding.
- [ ] Refactor SnKRLE and establish whether its greedy parser is optimal.

### 3. Graph-based implementation work

- [ ] Implement SLZ and SLZ24.
- [ ] Implement Compile/Puyo Puyo.
- [ ] Implement the Enigma optimal parser.
- [ ] Complete C42 encoding.
- [ ] Evaluate a shortest-output mode for Streets of Rage 2.

### 4. Multi-stream LZ implementations

- [ ] Complete and validate the shared multi-stream LZSS improvement.
- [ ] Implement Kid Chameleon.
- [ ] Implement Batman & Robin.

### 5. Research-heavy codecs

- [ ] Complete the Gunstar Heroes state-machine specification and decoder.
- [ ] Determine and implement the Gunstar encoder model.
- [ ] Complete the Nemesis formal model and literature review.
- [ ] Implement the selected Nemesis optimizer.
- [ ] Publish oracle comparisons and the supported optimality claim.

### 6. Integration

- [ ] Add public headers, libraries, tools, install/export rules, and package
      metadata for every newly exposed codec.
- [ ] Add managed-buffer entry points.
- [ ] Add legacy stream/file adapters where appropriate.
- [ ] Add C ABI functions and codec-specific option structures.
- [ ] Complete the new codec format, implementation, API-option, tool, README,
      and codec-matrix work tracked by
      [`documentation-plan.md`](documentation-plan.md).
- [ ] Run cross-platform static/shared builds and sanitizers.

## Deferred Decisions

- Final public identifiers and tool names for game-specific codecs.
- Whether C42 encoding exposes packed-only output or both packed and unpacked
  forms.
- Whether Gunstar's resumable step API is public.
- Whether Streets of Rage 2 exposes separate accurate and shortest modes.
- Whether Batman & Robin shared/global parameter streams are in first scope.
- Whether UFTC15 encoding is supported or only UFTC15 decoding.
- Whether a practical Nemesis optimizer can be proven global or is documented
  as oracle-tested and strictly improving.
