# Enigma Optimal Parsing Investigation and Development Plan

Status: proposed investigation and implementation plan. Codec changes remain
subject to the testing and buffer prerequisites in
[`codec-implementation-plan.md`](codec-implementation-plan.md).

This document expands the Enigma workstream in the parent plan. It separates
exact parsing for fixed header choices from heuristic header search and does
not claim globally optimal output until candidate completeness and container
costs have been proved or exhaustively validated.

## References and Provenance

Format analysis should begin with:

- the repository's
  [PDF capture of Sega Retro's Enigma compression
  page](<Enigma compression - Sega Retro.pdf>),
  revision `375031`;
- the [live Sega Retro page](https://segaretro.org/Enigma_compression);
- the current C++ decoder and encoder in
  [`src/lib/enigma.cc`](../src/lib/enigma.cc);
- the independent MC68000 decoder in
  [`src/asm/Enigma.asm`](../src/asm/Enigma.asm).

The PDF preserves the page's Creative Commons Attribution 4.0 notice and
revision identifier. Treat it as a versioned secondary source rather than a
normative specification. Reconcile every field, packet, count, termination,
and padding claim with both decoders and hand-authored streams before making
it normative.

## Objective

Replace the current greedy Enigma token selection with a reusable graph-based
encode plan that improves compression while preserving the wire format and
decoder compatibility.

Keep two concerns separate:

1. **Structural analysis:** identify useful constant and arithmetic structure
   in the source without requiring every identified span to be encoded as a
   separate packet.
2. **Encoding optimization:** for fixed header parameters, select the
   minimum-cost legal packets directly from the raw source, freely overriding
   every structural-analysis boundary.

Header selection is a third, outer optimization problem. The first exactness
target is the fixed-header parser. A bounded oracle must measure the gap
introduced by any header-candidate heuristic.

Plane maps around `0x2000` bytes, or 4096 16-bit words, are an important target
workload, but this is a corpus hypothesis rather than a public input limit.
Measure larger inputs and either support them with bounded resources or define
and validate an explicit format/API limit.

## Confirmed Current Behaviour

The investigation draft was formulated against an older codec revision. The
behaviour and active issues below have therefore been re-audited against the
current tree; older findings are retained only where they define useful
regression coverage.

The implementation in [`src/lib/enigma.cc`](../src/lib/enigma.cc) currently:

- reads the source as big-endian 16-bit words and silently ignores a trailing
  odd byte;
- derives the explicit-value high-bit mask from the OR of all input words;
- derives the remaining low-bit width with `std::bit_width`;
- selects the most frequent word as `common_value`;
- selects `incrementing_value` by scoring non-contiguous occurrences that
  match a progressively increasing expected value;
- greedily prioritizes incrementing, common, explicit `+1`/`-1`/constant
  arithmetic, and buffered inline packets in that order;
- emits a six-byte header, a seven-bit terminator, and a 16-bit-word-oriented
  bitstream;
- has no retained parse or exact pre-emission output-size plan.

The current source checks `position + 1 < unpack.size()` before reading the
next word, and `std::bit_width(0)` safely yields zero. Historical reports about
an unchecked final-word read and `slog2(0)` should become regression tests, not
unverified open defects.

The empty input path still requires an explicit contract: the common-value
selection dereferences the result of `max_element` on an empty map. Odd-sized
input also needs a defined reject-or-pad policy.

The format's four-bit count can represent 1 through 16 words for run packets.
The current encoder's `position + 15` loop limits appear to stop at 15 words.
Confirm the normative maximum against both decoders and add a focused test
before changing this behaviour.

The captured Sega Retro revision documents a starting-art-tile parameter that
is added to each decompressed word. The MC68000 entry point accepts this value
in `d0`, whereas the current C++ decoder exposes the unadjusted stream words
and has no equivalent parameter. Treat these as distinct interface semantics
until the public contract is decided; the parameter is not part of the
compressed six-byte header.

The same revision contains a complete compressed example and expected outputs
for starting-art-tile values `0x0000` and `0x1000`. Convert it into an
attributed fixture and validate both the raw C++ result and the adjusted
assembly result.

## Wire and Cost Model

Let:

- `x[i]` be the input words;
- `b` be the low explicit-value bit width from the header;
- `f` be the number of enabled high-bit flags;
- `q = b + f` be the bits used by one explicit value;
- `C` be the header `common_value`;
- `H0` be the initial header `incrementing_value`;
- `h` be the current incrementing value during decoding.

Validate these formulas against hand-authored decoder vectors and actual
emission before using them as graph weights.

### Inline packet

An inline packet carries 1 through 15 explicit words. The count value for 16 is
reserved by the `111` mode for the terminator:

```text
inline_cost(L) = 7 + qL,  1 <= L <= 15
```

For a logical uninterrupted inline span:

```text
inline_span_cost(L) = qL + 7 * ceil(L / 15)
```

### Explicit arithmetic packet

Explicit constant, increasing, and decreasing packets store one explicit
starting word and generate the rest with delta `0`, `+1`, or `-1`:

```text
arithmetic_cost(L) = 7 + q,  1 <= L <= 16
```

For a logical uninterrupted arithmetic span:

```text
arithmetic_span_cost(L) = (q + 7) * ceil(L / 16)
```

Length-one arithmetic packets are legal under the grammar but normally
dominated. Retain them in the oracle until the dominance rule is proved.
Arithmetic is modulo `2^16`.

### Common-value packet

A common packet emits 1 through 16 copies of `C` and does not change `h`:

```text
common_cost(L) = 6,  1 <= L <= 16
common_span_cost(L) = 6 * ceil(L / 16)
```

### Incrementing-value packet

When the source begins with `h, h + 1, ...`, an incrementing packet emits 1
through 16 words:

```text
incrementing_cost(L) = 6,  1 <= L <= 16
h' = h + L mod 2^16
```

This evolving value is the only history-dependent decoder state in the token
grammar.

### Container cost

Compare complete output size:

```text
total_bits =
    48-bit header
  + packet payload
  + 7-bit terminator
  + final padding to the bitstream's physical boundary
```

The current writer is backed by big-endian 16-bit units. Verify that the exact
size is:

```text
6 bytes + 2 * ceil((packet_bits + 7) / 16) bytes
```

for every alignment before adopting the formula. Exact planning must also
define empty input, odd source size, representable header fields, and any
permitted trailing compressed data.

## Architecture

Use four conceptually separate components:

1. **Semantic analyzer:** optional advisory annotations over the raw words.
2. **Header candidate generator:** proposes `C`, `H0`, and, if later shown
   profitable, alternative explicit-value mask/width choices.
3. **Fixed-header parser:** exact stateful DAG over the raw words.
4. **Encode plan and lowering:** retains the selected logical path, exact size,
   and header so commit performs no search.

The semantic analyzer is not a correctness dependency. Benchmark candidate
quality and runtime with and without it; remove or simplify it if direct raw
source analysis performs as well.

Initially freeze the current derivation of the explicit-value high-bit mask and
low-bit width. Audit whether other valid mask/width combinations can lower
`q`. If so, promote them into header-candidate search only after the token and
`C`/`H0` parser is validated.

Always retain the complete current encoder output as a candidate during
migration. Reconstructing its header choices inside the new parser is useful
but is not an exact non-regression guarantee until byte equivalence is proved.

## Semantic Structural Analysis

Treat this pass as a lexer or annotator, not as a compressor. Emit unlimited
logical spans such as:

```cpp
struct inline_span {
    std::size_t start;
    std::size_t length;
};

struct arithmetic_span {
    std::size_t start;
    std::size_t length;
    int delta; // -1, 0, or +1
};
```

Compute adjacent word differences modulo `2^16` and recognize deltas `-1`, `0`,
and `+1`. A span of `L` words explains `L - 1` adjacencies, so a suitable
lexicographic objective is:

1. maximize explained arithmetic adjacencies;
2. prefer longer coherent spans;
3. use fewer arithmetic spans;
4. merge residual words into maximal inline annotations;
5. apply deterministic left-to-right and delta tie-breakers.

Maximal difference runs can share endpoint words. For example, `1 2 2 2`
contains a `+1` relation ending at the first `2` and a constant relation
starting there. Resolve ownership deterministically, retain diagnostics for
ambiguous boundaries, and compare the selected annotations with a small
exhaustive structural oracle.

Do not impose the physical 15- or 16-word limits here. A 50-word progression is
one semantic span. Physical limits belong to the final graph edges or lowering.

Most importantly, annotations may prioritize candidates and edge generation
but must never veto a legal final packet. The fixed-header parser may absorb a
short arithmetic annotation into inline data, split it, or discover a special
packet inside a region annotated as inline.

## Common-Value Candidates

Every common packet requires its header value to equal the encoded source
words. Therefore all useful `C` values come from the source; an unused common
mode can use a canonical arbitrary header value.

A "disabled common" sentinel is a search policy that suppresses common edges,
not a distinct wire value. Keep that distinction explicit when comparing
candidate states.

Rank common candidates using:

- semantic constant spans;
- raw maximal equal-value runs;
- occurrences in residual or other arithmetic regions;
- packet opportunities and extraction savings rather than frequency alone.

For one constant arithmetic packet, replacing the explicit form with a common
packet saves `q + 1` bits, whether the packet contains 1 or 16 words. Frequency
alone consequently overvalues scattered occurrences and undervalues packet
boundaries.

Extracting a special run of length `k` from an inline interval with residual
lengths `a` and `b` changes inline packetization. Define:

```text
m = ceil((a + k + b) / 15) - ceil(a / 15) - ceil(b / 15)
```

For `k <= 16`, the estimated saving of a common packet over leaving those words
inline is:

```text
common_gain = qk + 7m - 6
```

This formula is useful for ranking, but only the exact fixed-header parse may
select the winner.

Start with:

1. the current most-frequent value;
2. top packet-weighted candidates;
3. values from the longest constant spans;
4. a disabled-mode policy;
5. all distinct source values in the bounded oracle.

Measure recall: how often does the restricted set contain the oracle-optimal
`C`? Expand or replace the heuristic when it misses.

## Initial Incrementing-Value Candidates

An incrementing opportunity is an interval `(s, e, v)` satisfying:

```text
x[s + j] = v + j mod 2^16,  0 <= j < e - s
```

After using it, the expected value becomes `v + (e - s)`. Non-incrementing
packets between opportunities leave this state unchanged, so separated source
intervals may form one compatible chain:

```text
100 101 102 103  ...  104  ...  105 106 107
```

The search must permit:

- skipping an early compatible occurrence;
- consuming a non-maximal prefix of an increasing run;
- using a locally unprofitable singleton as a bridge;
- starting at any useful source value;
- retaining distinct `(position, next_value)` states until dominance is safe.

Useful opportunities may occur inside any semantic span. Index raw positions
by word value to find later occurrences of the expected value without scanning
all remaining input.

For a fixed `H0`, the exact parser starts at `h = H0`. As a separate search
strategy, investigate an uncommitted virtual state `h = bottom`: the first
incrementing edge commits `H0` to its starting word. If no incrementing edge is
used, lower to a canonical unused header value. This can consider every useful
initial value without running a separate fixed-header parse for each, but it
changes the state model and must be checked against explicit enumeration.

As with common mode, a disabled-incrementing sentinel is parser policy rather
than an on-wire value.

Do not prune based only on remaining word count or chain length. Gain depends
on `q`, residues modulo 15 and 16, extraction costs, and later bridges. Begin
without aggressive pruning, record actual state counts, and require a proved
upper bound or exhaustive validation for each dominance rule.

## Exact Fixed-Header Parser

For fixed explicit-value representation, `C`, and `H0`, use DAG states:

```text
(i, h)
```

where `i` is the next source position and `h` is the current incrementing
value. Every edge consumes at least one word, so process states in increasing
`i`; general Dijkstra machinery is unnecessary.

Generate physical packet edges directly from raw data:

- inline lengths 1 through `min(15, remaining)`, cost `7 + qL`, state unchanged;
- explicit delta `-1`, `0`, or `+1` lengths 1 through 16 while legal, cost
  `7 + q`, state unchanged;
- common lengths 1 through 16 while all words equal `C`, cost 6, state
  unchanged;
- incrementing lengths 1 through 16 while words start at `h` and increase,
  cost 6, next state `h + L`.

Bounded physical edges make cost accounting and parent reconstruction direct.
The reconstructed path may coalesce adjacent compatible physical packets into
unlimited logical spans for diagnostics and the retained plan. Alternatively,
logical-span edges using the ceiling formulas are valid only if endpoint
generation is complete and their lowering preserves predicted cost. Evaluate
that optimization after the bounded graph is correct.

Non-maximal edges are required. An increasing sequence may need to stop before
a repeated value becomes a profitable common run, and an incrementing packet
may need to stop early so its resulting `h` matches a later bridge.

For fixed `H0`, reachable `h` values are constrained by the cumulative number
of words emitted through incrementing packets; do not allocate or iterate all
65,536 values blindly. Candidate representations include sparse maps, sorted
label vectors, or dense arrays with generation counters. Prove any state
merging rule against the exhaustive fixed-header oracle.

Use deterministic path ordering:

1. fewer complete output bytes after terminator and padding;
2. fewer packet bits;
3. fewer packets;
4. fewer explicit values;
5. a documented packet-family and longest-edge order;
6. lexicographic header fields when comparing header candidates.

Because final padding is global, the DAG may minimize packet bits first and
apply the remaining rules after container rounding. Retain the exact packet-bit
count in the plan.

## Arithmetic Extraction Bounds

When an explicit arithmetic run of length `k <= 16` is extracted from an
otherwise inline interval with residual lengths `a` and `b`, use:

```text
m = ceil((a + k + b) / 15) - ceil(a / 15) - ceil(b / 15)
arithmetic_gain = q(k - 1) + 7(m - 1)
```

This yields the useful ranking thresholds:

| `m` | Inline structural effect | Arithmetic is profitable when |
| ---: | --- | --- |
| -1 | One extra inline packet | `q(k - 1) > 14` |
| 0 | Inline packet count unchanged | `q(k - 1) > 7` |
| 1 | One inline packet eliminated | Any `k >= 2` |
| 2 | Two inline packets eliminated | Any `k >= 2` |

These are candidate-generation and diagnostic bounds, not permission to prune
legal graph edges until their assumptions and equality cases are validated.
The semantic analyzer should recognize the progression regardless of local
profitability.

## Header Search and Exactness Boundary

For common packets that are actually used, `C` must be a source value. For the
first incrementing packet that is used, `H0` must equal the source word at that
packet's start. Distinct source values plus canonical unused-mode policies
therefore provide a finite exhaustive set for the two special values.

The Cartesian product may still be too expensive for production inputs. Build
and compare these strategies:

1. current header choices plus exact fixed-header parsing;
2. top `KC` common and top `KH` incrementing candidates;
3. a small Cartesian product with disabled-mode policies;
4. uncommitted incrementing-state search for each common candidate;
5. iterative refinement or joint search only if oracle gaps justify it.

For bounded inputs, enumerate every relevant `(C, H0)` pair and every legal
packet path. This distinguishes:

- fixed-header parsing errors;
- missed common candidates;
- missed incrementing candidates;
- interactions between the two header fields;
- any later explicit mask/width optimization gap.

Describe production output as fixed-header optimal or oracle-tested improving
until candidate pruning is proved complete. Retain the current encoded stream
as a final candidate so the selected byte size cannot regress during rollout.

## Physical Lowering and Encode Plan

The winning plan should retain:

```cpp
struct enigma_encode_plan {
    enigma_header header;
    std::size_t input_words;
    std::size_t packet_bits;
    std::size_t output_bytes;
    std::vector<enigma_packet> packets;
};
```

The concrete types may differ, but commit must not repeat structural analysis,
candidate generation, or graph search.

If the path uses logical spans, lower them canonically:

- inline spans into packets of at most 15 words;
- arithmetic, common, and incrementing spans into packets of at most 16 words.

Left-packed lowering is a reasonable tie-breaker, but verify that it preserves
the predicted bit count and incrementing state. Emission must reproduce the
plan's exact size, header, terminator, and padding.

## Development Phases

### Phase 1: Format audit and baseline

- [ ] Reconcile the Sega Retro description with the current C++ and MC68000
      decoders, recording discrepancies and source revisions.
- [ ] Convert the published example into an attributed fixture for starting
      art tiles `0x0000` and `0x1000`.
- [ ] Specify whether each public decoder returns raw stream words or applies
      the external starting-art-tile adjustment.
- [ ] Write hand-authored decoder vectors for every packet family and count.
- [ ] Confirm the 15-word inline and 16-word run maxima.
- [ ] Verify modulo-`2^16` arithmetic and incrementing-state updates.
- [ ] Define empty and odd-sized input behaviour.
- [ ] Retain regressions for safe final-word lookahead and all-zero input.
- [ ] Measure the current greedy output and round trips on the corpus.
- [ ] Verify analytical packet, terminator, padding, and total-size formulas
      against actual streams.

### Phase 2: Isolated representations and exact cost

- [ ] Introduce explicit header, packet, and encode-plan types.
- [ ] Separate current header selection, greedy parsing, and emission.
- [ ] Implement a pure exact-size function.
- [ ] Re-emit the current greedy path byte for byte through the new plan.
- [ ] Preserve the complete legacy encoder as a fallback candidate.

### Phase 3: Semantic analyzer and diagnostics

- [ ] Implement difference-run detection and overlap resolution.
- [ ] Emit unlimited arithmetic and residual-inline annotations.
- [ ] Define deterministic semantic tie-breaking.
- [ ] Measure coverage, ambiguity, candidate recall, and runtime.
- [ ] Compare candidate quality with direct raw-source generation.

### Phase 4: Exact fixed-header DAG

- [ ] Generate every legal bounded physical edge from raw words.
- [ ] Retain and reconstruct the selected path.
- [ ] Validate every fixed-header result against exhaustive enumeration on
      bounded inputs.
- [ ] Verify predicted and emitted sizes and decoder output.
- [ ] Profile reachable incrementing states before optimizing storage.

### Phase 5: Common and incrementing candidate search

- [ ] Add packet-weighted common ranking and disabled-mode policy.
- [ ] Add the raw value-position index and incrementing opportunities.
- [ ] Compare explicit `H0` enumeration with the uncommitted-state search.
- [ ] Support partial increasing runs and singleton bridges.
- [ ] Measure candidate recall against exhaustive header pairs.

### Phase 6: Production search and integration

- [ ] Choose deterministic `KC`, `KH`, state, time, and memory bounds from
      measurements.
- [ ] Include current header choices and complete legacy output.
- [ ] Retain the selected parse and exact size through prepare/commit.
- [ ] Integrate checked memory cursors and managed-buffer negotiation.
- [ ] Publish the supported optimality level and known gaps.

### Phase 7: Evidence-driven optimization

- [ ] Precompute maximal delta-run lengths and value positions.
- [ ] Deduplicate only proved-dominated endpoints and states.
- [ ] Evaluate residue-aware range relaxations or logical-span edges.
- [ ] Add safe upper-bound pruning only after oracle validation.
- [ ] Audit alternate explicit-value mask/width choices.
- [ ] Re-run corpus, oracle, sanitizer, and performance suites after every
      pruning change.

## Validation Strategy

### Correctness and format boundaries

- all four packet families and explicit arithmetic deltas;
- counts 1, 2, 14, 15, and 16 where legal;
- packet transitions across every 16-bit output alignment;
- terminator immediately after each packet family;
- zero-bit explicit payload and every high-bit flag combination;
- arithmetic wrap at `0xffff -> 0x0000` and the reverse;
- equal `C` and `h`, with both legal packet choices retained;
- empty, one-word, odd-byte, and larger-than-typical input contracts;
- exact header values, packet bits, output bytes, and decoded bytes.

### Adversarial parsing cases

Construct sequences where:

- a short arithmetic span is best absorbed into inline data;
- a non-maximal arithmetic prefix exposes a common run;
- extracting a special packet changes inline packet count near 15 words;
- a 16-word run demonstrates the current boundary difference;
- skipping the earliest incrementing match reaches a better chain;
- a singleton incrementing packet bridges two profitable runs;
- stopping an incrementing run early creates a better later state;
- a frequent scattered word loses to a less frequent packet-coherent common
  value;
- current `C` or `H0` selection misses the best fixed-header result;
- two paths tie in bytes after padding but differ in packet bits.

### Oracles

Build two independent references for small inputs:

1. a fixed-header packet enumerator that validates the stateful DAG;
2. a joint header-and-packet enumerator over relevant source values.

Use decoder-backed verification for every oracle result. Test exhaustive small
alphabets, deterministic randomized words, and targeted wraparound values.
Candidate recall and every pruning rule must be measured against the joint
oracle.

### Compression and performance

Compare:

- the complete current encoder;
- current headers with exact token parsing;
- semantic candidates with greedy packetization;
- improved `C` search;
- improved `H0` or uncommitted-state search;
- the production candidate-pair portfolio;
- the joint oracle within its bound.

Record compressed bytes, packet bits, percentage change, runtime, chosen
headers, candidate counts, states per position, transition counts, peak memory,
and fallback frequency. Include blank-heavy maps, contiguous tile sequences,
repeated rows, attribute-heavy/noisy maps, long arithmetic runs, bridge
patterns, 15/16-boundary cases, synthetic adversarial inputs, and legally
redistributable representative plane maps.

## Success Criteria

The first production implementation succeeds when it:

1. emits valid streams accepted by the C++ and independent assembly decoders;
2. defines empty and odd-sized input behaviour;
3. predicts exact output size and commits without rerunning the search;
4. is fixed-header optimal within the audited packet grammar;
5. matches the joint oracle within its declared bound or reports candidate
   misses explicitly;
6. never selects output larger than the retained legacy candidate;
7. improves at least one documented representative or compelling adversarial
   case;
8. produces deterministic output for fixed options;
9. has measured and acceptable time and memory use;
10. documents whether its result is fixed-header optimal, oracle-tested, or
    globally optimal.

The recommended first implementation is:

```text
audited packet grammar and exact size model
+ bounded physical-edge DAG for fixed headers
+ current and packet-weighted header candidates
+ uncommitted incrementing-state experiment
+ retained legacy output fallback
```

Semantic annotations, logical-span acceleration, aggressive pruning, and
explicit mask/width search should remain evidence-driven enhancements.
