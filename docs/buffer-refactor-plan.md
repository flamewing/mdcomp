# Buffer-Oriented Codec Refactor

Status: design agreed; implementation blocked by the testing framework.

> [!IMPORTANT]
> Establish the existing-codec regression baseline from
> [`testing-framework-plan.md`](testing-framework-plan.md) before starting this
> refactor. The tests must capture current round-trip, known-answer decoding,
> and format-edge behaviour so changes to the I/O model can be distinguished
> from codec regressions. Final public C++ and C usage documentation is tracked
> by [`documentation-plan.md`](documentation-plan.md) and should be written
> against the stable interfaces produced here.

This document describes the refactor that will move mdcomp's compression and
decompression algorithms away from stream-based I/O and onto checked memory
buffers. It also serves as the implementation checklist.

## Goals

The refactor should:

1. Keep file and stream I/O at API boundaries. Codec algorithms should only
   access memory.
2. Accept caller-provided input and output buffers.
3. Allocate or reallocate owned output buffers to the exact required size.
4. Detect insufficient fixed buffers before writing and never truncate output
   silently.
5. Negotiate and validate buffer alignment, including for borrowed storage and
   memory-mapped files.
6. Support a stable C ABI based on opaque buffer handles.
7. Preserve the existing stream APIs as compatibility adapters.
8. Support moduled formats without recomputing each module's compression parse.

## Design Decisions

### Codec lifecycle

Each codec operation is divided into two phases:

1. **Prepare**
   - Lock and validate the source buffer.
   - Parse or analyse the source.
   - Validate malformed input and checked size arithmetic.
   - Determine the exact output size and required alignment.
   - Retain any work needed by the write phase, such as an LZSS parse.
2. **Commit**
   - Prepare the destination for the exact size and alignment.
   - Lock the destination to obtain a stable writable view.
   - Write the prepared result through checked memory cursors.
   - Verify that the number of bytes written matches the prepared size.

No destination span may be obtained before the final resize or remapping
operation.

### Results and errors

Replace direction-specific or Boolean results with a direction-neutral result:

```cpp
enum class codec_status {
    ok,
    output_too_small,
    buffer_locked,
    misaligned_input,
    misaligned_output,
    incompatible_module_alignment,
    module_size_mismatch,
    invalid_input,
    size_not_representable,
    size_overflow,
    allocation_failure,
    io_error,
    invalid_argument,
    internal_error,
};

struct codec_result {
    codec_status status;
    size_t input_consumed;
    size_t output_size;
    size_t required_output_size;

    // Populated for buffer and moduled errors when applicable.
    size_t required_alignment;
    size_t address_modulus;
    size_t error_offset;
    size_t module_index;
};
```

Expected errors use status values rather than exceptions. In particular, an
undersized fixed destination returns `output_too_small` and the exact required
size without changing any destination byte. Exceptions may still be used
internally for allocation and system failures, but boundary code must translate
them to statuses.

All encoded or decoded byte counts are logical codec sizes. File-only padding
is handled and reported separately by file and legacy stream adapters.

### Managed buffers

A managed buffer represents owned, borrowed, or mapped storage. It does not
expose a naked `get()` operation.

The principal operations are:

```cpp
lock_read(buffer_requirements)
prepare_output(exact_size, required_alignment)
lock_write(buffer_requirements)
```

`lock_read` and `lock_write` return a move-only RAII locked-view object. The
view contains a bounded `std::span`, retains or pins the backing storage, and
automatically unlocks when destroyed. Callback helpers such as
`with_read_lock` and `with_write_lock` should be the preferred interface when
practical.

Locking rules:

- Multiple read views may coexist.
- A write view is exclusive.
- Any active view prevents resizing, reallocating, remapping, closing, or
  replacing its storage.
- A conflicting mutation returns `buffer_locked`; it must not block or
  deadlock.
- Owned and mapped views retain the backing state for their lifetime.
- A borrowed view cannot extend the lifetime of external memory; the caller
  must keep it alive until the view is released.
- Spans and pointers obtained from a view are invalid after the view is
  released.

The implementation should use a custom pin/lease token rather than placing a
literal `std::scoped_lock` in the returned object. Holding the internal mutex
for the entire algorithm would prevent a mutation attempt from returning
`buffer_locked`.

### Size and alignment negotiation

Buffer requirements keep these independent:

- logical byte size;
- memory address alignment;
- format input granularity or normalization;
- inter-module padding;
- boundary-only file padding.

Alignment requirements must be nonzero powers of two. Alignment is checked
against the final pointer after allocation or mapping. Empty views are valid
without dereferencing their pointer.

Borrowed buffers validate their existing address and capacity. Owned buffers
allocate or reallocate with the requested alignment. Mapped buffers validate
the address returned by the operating system.

Comper encoding requests `alignof(uint16_t)` for its input before creating a
word-oriented view. A misaligned borrowed span or mapped-file offset returns
`misaligned_input`; it is not silently copied. Odd-length normalization remains
a separate format rule.

### Memory cursors

Replace stream reads, writes, seeking, and bitstream access inside codecs with
bounded memory cursors:

- checked byte and endian reads;
- checked byte and endian writes;
- checked descriptor-bit readers and writers;
- explicit current offset and remaining size;
- overlapping LZ back-reference expansion with bounds validation;
- checked addition, multiplication, and round-up helpers.

A failed read or write must not advance the cursor. Decoder results report the
actual number of compressed bytes consumed rather than the size of the entire
source view.

Make the span-backed cursor operations, endian accessors, descriptor-bit
readers and writers, LZSS edge value accessors, and adaptor edge coding paths
`constexpr` wherever their successful and error paths permit it. This scope is
the allocation-free codec core; managed-buffer locking, allocation, mapping,
and legacy stream adapters are runtime boundaries and do not need to be
constant-evaluable.

The LZSS adaptor contract must permit a small fixed-capacity,
constant-evaluable edge collector. In particular, `decode_edge` must not
require `std::list` merely to append one decoded edge. Descriptor writers must
also expose deterministic finalization so a constexpr test can encode a single
edge into fixed storage and then construct a reader over the exact bytes and
descriptor state that were produced.

This also includes `output_edge`: replace its stream seeking, copying, and
filling with the checked output cursor and make its allocation-free call graph
constant-evaluable. The cursor must support constexpr overlapping
back-reference expansion so dictionary edges can be tested against real
history, not only compared as metadata.

Together these changes enable the testing plan's compile-time proof that, for
every encoder-supported edge form and boundary parameter, `decode_edge` is a
semantic left inverse of `encode_edge` and both the original and decoded edge
have identical `output_edge` behaviour. The check includes the edge type and
payload, produced bytes, decompressed-size change, termination result, and
exact compressed input consumption. It deliberately excludes incidental
container ownership and compressed-stream positions. Position-dependent
formats such as Rocket and Saxman must run the check with the same explicit
starting decompressed position and identical initialized output history.
Decoder-only and non-canonical encodings remain covered by independent decode
tests rather than by an invalid byte-for-byte inverse claim.

### Memory-mapped files

Boost mapping types belong in a private boundary implementation, not in common
public codec headers.

A writable mapped buffer may initially contain only a path. When
`prepare_output` requests a size, it:

1. Rejects the operation if a view is currently locked.
2. Closes any old mapping.
3. Checks that the size is representable by the platform file-offset type.
4. Creates or truncates the file to the exact stored size.
5. Opens a writable mapping over that size.
6. Validates the returned address against the requested alignment.
7. Returns a lockable mapped buffer.

Growing or shrinking repeats the close, resize, and remap sequence. A
zero-length output is truncated using ordinary file handling and represented
by an empty view because it cannot have a normal nonempty mapping.

Read-only mappings never expose mutation. Source offsets are applied before
alignment validation, so an otherwise page-aligned mapping can still fail a
codec requirement when the logical start offset is misaligned.

Boost.Iostreams must be linked privately by the file-boundary target. Merely
adding the package to `vcpkg.json` is not sufficient.

### Legacy I/O boundaries

Existing stream entry points remain available. They:

1. Read the source into appropriately aligned owned storage.
2. Invoke the memory-based codec operation.
3. Write the completed output to the destination stream.
4. Apply existing boundary-only padding where required.

File APIs use mappings when possible and an owned-buffer fallback when mapping
is unavailable. Empty files, offsets at EOF, same-file input/output, and mapping
failures require explicit handling.

### Moduled decoding

The moduled header provides the full decompressed size, so decoding can prepare
the complete destination before processing individual modules:

1. Lock the compressed source and read the full-size metadata.
2. Validate the metadata and prepare the parent destination for the exact size
   and alignment.
3. Lock the parent destination for the duration of module output.
4. For every module, create a borrowed fixed-size child buffer:
   - `ModuleSize` bytes for full modules;
   - the remaining byte count for the final module.
5. Decode into the child buffer and require the module to produce exactly the
   expected byte count.
6. Advance by the module's reported compressed `input_consumed`.
7. Round the compressed payload offset to `module_padding` with checked
   arithmetic before decoding the next module.

The child buffer retains or references the parent lock token; copying a span
alone is insufficient. Child input and output addresses are independently
checked against codec alignment requirements.

Errors include the module index, compressed offset, decompressed offset, and
underlying codec status. An invalid later module may leave earlier modules
written, but the result reports the number of committed bytes. Capacity and
alignment failures are detected before the first module write.

Unless the format definition is deliberately changed, zero-length input
continues to contain one encoded empty module to preserve existing behaviour.

### Moduled encoding

Moduled encoding must not run each module's optimal-parse search twice. The
prepare phase retains the selected codec plan for every module:

```cpp
template <typename CodecPlan>
struct prepared_module {
    size_t input_offset;
    size_t input_size;
    size_t output_offset;
    size_t encoded_size;
    size_t padded_end;
    CodecPlan codec_plan;
};

template <typename CodecPlan>
struct moduled_encode_plan {
    size_t uncompressed_size;
    size_t encoded_size;
    size_t required_alignment;
    std::vector<prepared_module<CodecPlan>> modules;
};
```

Preparation:

1. Lock the complete source.
2. Validate that its size fits the moduled metadata field; reject overflow
   instead of masking it to 16 bits.
3. Split the source into `ModuleSize` chunks.
4. Run `Format::prepare_encode` exactly once for each chunk.
5. Move each prepared codec plan into the moduled plan.
6. Release temporary parse-search structures after each selected path has
   been retained.
7. Calculate module output offsets, inter-module padding, aggregate alignment,
   and total encoded size with checked arithmetic.
8. Prepare and lock the final destination only after every module succeeds.

Commit writes the size header, commits each retained module plan directly into
its exact output subspan, and fills inter-module padding with zeroes. Every
module must write its planned byte count.

The retained LZSS representation should contain only the selected path, not the
complete dynamic-programming graph. A compact `std::vector` path is preferred
over the current `std::list`. If plan nodes contain source spans, the parent
source lock remains alive through commit. Replacing those spans with source
offsets is a possible later improvement.

Module padding is calculated relative to the payload start after the size
header, matching the current intermediate-buffer behaviour. Each module output
offset must satisfy the child codec's alignment requirement. If the fixed
container layout makes that impossible, return
`incompatible_module_alignment`.

### C ABI

Add a standalone C-compatible header and a central `mdcomp_c` library.

`mdcomp_buffer` remains opaque. C callers can create a handle, pass it to codec
functions, obtain a scoped read-only view of completed data, and destroy it.
They cannot inspect or mutate the buffer's size, capacity, alignment, locks, or
storage implementation directly.

Creation functions construct handles around one of these boundary resources:

- library-owned aligned, resizable storage, primarily for codec output;
- caller-owned read-only memory;
- caller-owned writable fixed memory;
- a mapped input file and starting offset;
- a deferred-size mapped output file.

The buffer creation and destruction surface is intentionally limited to
functions shaped like:

```c
mdcomp_status mdcomp_buffer_create_borrowed_input(
        const void* data, size_t size, mdcomp_buffer** result);
mdcomp_status mdcomp_buffer_create_borrowed_output(
        void* data, size_t capacity, mdcomp_buffer** result);
mdcomp_status mdcomp_buffer_create_resizable(
        mdcomp_buffer** result);
mdcomp_status mdcomp_buffer_create_mapped_input(
        const char* path, size_t offset, mdcomp_buffer** result);
mdcomp_status mdcomp_buffer_create_mapped_output(
        const char* path, mdcomp_buffer** result);
mdcomp_status mdcomp_buffer_destroy(mdcomp_buffer** buffer);
```

The exact path-string ABI will be fixed before the C header is implemented.
Borrowed-memory callers already own their pointer, while mapped-output callers
consume the named file.

Codec entry points take a `const mdcomp_buffer*` source and
`mdcomp_buffer*` destination, return `mdcomp_status`, and fill a C result
structure. They perform all requirement negotiation, resizing, alignment
validation, locking, and unlocking internally. The result reports the logical
output size so a caller knows how much of a borrowed destination is valid.

#### Read-only output views

A library-owned resizable result must be usable by its caller. Provide an
opaque, read-only view that pins the completed allocation:

```c
mdcomp_status mdcomp_buffer_read_view_create(
        const mdcomp_buffer* buffer,
        mdcomp_buffer_read_view** result);
const void* mdcomp_buffer_read_view_data(
        const mdcomp_buffer_read_view* view);
size_t mdcomp_buffer_read_view_size(
        const mdcomp_buffer_read_view* view);
void mdcomp_buffer_read_view_destroy(
        mdcomp_buffer_read_view** view);
```

The view exposes only a constant pointer and the logical data size. It does not
expose capacity, alignment, mutability, mapping controls, or the underlying
buffer type.

The pointer is valid only while the read view exists. While a read view is
active:

- the buffer cannot be used as a codec destination;
- it cannot be resized, remapped, or destroyed;
- it may still be used as a codec source because read locks can coexist;
- conflicting operations return `buffer_locked`.

Destroying the read view releases the pin and invalidates all pointers obtained
from it. `mdcomp_buffer_destroy` takes a pointer to the handle, returns
`buffer_locked` if a view or codec operation is active, and clears the caller's
handle on success.

For zero-length data, the view reports size zero and may return a null data
pointer. A display or foreign API that retains the pointer asynchronously must
also retain the read-view handle until that API has finished using the data.

There remain deliberately no C functions for:

- obtaining a mutable pointer to library-owned storage;
- querying or changing capacity or alignment;
- changing the buffer's logical size;
- acquiring a write lock;
- resizing or remapping directly;
- inspecting the C++ implementation type.

Codec calls are synchronous. A caller must not destroy a handle or mutate
borrowed storage concurrently with a codec operation. Concurrent codec calls
that conflict on a handle return `buffer_locked`. Destruction releases
library-owned and mapped resources but never frees caller-owned borrowed
storage.

No STL types, references, exceptions, or C++ `bool` cross the ABI.

## Implementation Checklist

### 0. Baseline and design

- [x] Review the partial refactor in `diff.log`.
- [x] Record the agreed architecture and implementation checklist.
- [ ] Complete the existing-codec blocking baseline in
      [`testing-framework-plan.md`](testing-framework-plan.md).
- [ ] Confirm that its C++, known-answer, format-edge, and assembly-decoder
      suites pass against the pre-refactor implementation.
- [ ] Document which padding behaviours are part of a codec format and which
      are only file/legacy boundary conventions.
- [ ] Audit all on-disk size fields and document their valid ranges and
      zero-value semantics.

### 1. Core result and arithmetic types

- [ ] Add `codec_status`, `codec_result`, and structured buffer diagnostics.
- [ ] Add checked addition, multiplication, conversion, and round-up helpers.
- [ ] Replace silent size masking with `size_not_representable` errors.
- [ ] Define byte-view aliases and remove `char`/`uint8_t` mismatches from the
      new APIs.

### 2. Managed buffers and locked views

- [ ] Define buffer size, alignment, access, and mutability requirements.
- [ ] Implement the move-only read and write locked-view types.
- [ ] Implement reader/writer pin tracking and `buffer_locked` mutations.
- [ ] Add owned aligned storage with alignment-preserving reallocation.
- [ ] Add borrowed read-only and borrowed fixed-output buffers.
- [ ] Add parent-retaining borrowed sub-buffers for moduled operations.
- [ ] Add callback-style locked-view helpers.
- [ ] Test view lifetime, conflicting locks, mutation rejection, and alignment
      diagnostics.

### 3. Checked memory I/O

- [ ] Implement bounded input and output byte cursors.
- [ ] Add endian read/write operations for memory cursors.
- [ ] Adapt descriptor bit buffers to memory callbacks.
- [ ] Make the allocation-free cursor, endian, descriptor-bit, and checked
      arithmetic operations usable during constant evaluation.
- [ ] Provide deterministic descriptor finalization over fixed storage so an
      encoded edge can be read back in the same constant expression.
- [ ] Implement constexpr-capable validated overlapping LZ back-reference
      copying and fixed-output filling.
- [ ] Ensure failed operations do not advance cursors.
- [ ] Add unit tests for truncation, overflow, invalid distance, and exact-end
      reads and writes.

### 4. Codec prepare/commit framework

- [ ] Replace `basic_decoder` internals with a direction-neutral codec facade.
- [ ] Define reusable encode and decode plan conventions.
- [ ] Add span, owned-buffer, and borrowed-buffer overloads.
- [ ] Keep legacy stream overloads as boundary adapters.
- [ ] Ensure fixed-capacity failure occurs before any destination write.
- [ ] Ensure derived codecs publicly expose the intended facade overloads
      without C++ name hiding.

### 5. LZSS codecs

- [ ] Return a reusable selected-path plan from `find_optimal_parse`.
- [ ] Store the selected path compactly instead of using `std::list`.
- [ ] Generalize the decoded-edge sink so `decode_edge` can append to a
      C++20 constexpr-compatible fixed-capacity collector.
- [ ] Keep every adaptor's allocation-free `encode_edge`, `decode_edge`, and
      `output_edge` call graph constant-evaluable.
- [ ] Use the prepared compressed size to allocate before encoding.
- [ ] Parse and size decoder output before committing edges.
- [ ] Convert Comper alignment assumptions into explicit buffer requirements.
- [ ] Migrate Comper and ComperX.
- [ ] Migrate Kosinski and Kosinski Plus.
- [ ] Migrate LZKN1.
- [ ] Migrate Rocket.
- [ ] Migrate Saxman.
- [ ] Add the testing plan's compile-time semantic
      `decode_edge(encode_edge(edge))` checks for every encoder-supported edge
      form and its minimum and maximum parameters, including equivalent
      `output_edge` bytes from identical initialized histories.
- [ ] Verify compressed `input_consumed` at terminators and descriptor
      boundaries.

### 6. Other codecs

- [ ] Migrate SNKRLE using header-derived decode sizing and exact encode
      counting.
- [ ] Migrate Enigma using a sizing pass or reusable token plan.
- [ ] Migrate Nemesis using memory builders for its candidate encodings.
- [ ] Audit and migrate ArtC42 and any remaining stream-based algorithms.
- [ ] Preserve format-specific normalization and metadata validation.

### 7. Moduled adaptor

- [ ] Define module-indexed diagnostics and checked payload-offset handling.
- [ ] Implement full-destination preparation for moduled decode.
- [ ] Decode through parent-retaining fixed child buffers.
- [ ] Validate exact output size and input progress for every decoded module.
- [ ] Define and implement the reusable `moduled_encode_plan`.
- [ ] Retain each module's prepared codec plan and call module preparation only
      once.
- [ ] Derive aggregate destination size and alignment from all module plans.
- [ ] Commit modules directly to their planned subspans.
- [ ] Zero-fill inter-module padding deterministically.
- [ ] Preserve and test the zero-length container behaviour.

### 8. File, stream, and mmap boundaries

- [ ] Implement the read-only mapped input buffer.
- [ ] Implement deferred-open mapped output preparation.
- [ ] Create or truncate mapped output files to their exact requested size.
- [ ] Implement close, resize, remap, and post-map alignment validation.
- [ ] Handle zero-length mapped output.
- [ ] Reject remapping while a view is locked.
- [ ] Link Boost.Iostreams privately rather than exposing it from codec headers.
- [ ] Add owned-buffer fallback when mapping is unavailable.
- [ ] Handle source offsets, EOF offsets, and same-file input/output.
- [ ] Preserve legacy stream and command-line behaviour.

### 9. C ABI

- [ ] Define the standalone C status, result, and opaque handle declarations.
- [ ] Finalize the path-string ABI.
- [ ] Implement library-owned resizable, borrowed, and mapped buffer
      constructors.
- [ ] Implement the opaque read-view handle with constant data and logical-size
      accessors.
- [ ] Pin backing storage for the complete lifetime of every C read view.
- [ ] Reject destination use, resizing, remapping, and buffer destruction while
      a C read view is active.
- [ ] Verify the exported C API contains no mutable data, capacity, alignment,
      write-lock, resize, remap, or type-inspection functions.
- [ ] Keep all buffer negotiation and locked-view use inside codec entry
      points, apart from the restricted read-view lease.
- [ ] Add per-codec encode/decode entry points and codec-specific options.
- [ ] Catch and translate every exception at the ABI boundary.
- [ ] Add symbol export/versioning support for Windows and ELF platforms.
- [ ] Compile the public header as C, not only as C++.
- [ ] Add at least one external-language FFI smoke test.

### 10. Validation and completion

- [ ] Test exact, oversized, one-byte-short, and zero-capacity destinations.
- [ ] Verify undersized fixed buffers remain byte-for-byte unchanged.
- [ ] Test deliberately misaligned borrowed and mapped views.
- [ ] Test owned-buffer alignment across reallocations.
- [ ] Test mmap creation, growth, shrinkage, remapping, and empty files.
- [ ] Test moduled sizes around every `ModuleSize` boundary.
- [ ] Test malformed data in early and late modules.
- [ ] Run the golden corpus through every supported buffer boundary.
- [ ] Run ASan and UBSan builds on malformed-input tests.
- [ ] Verify static and shared builds on Windows and Linux.
- [ ] Supply the stable public contracts and tested examples required by the
      C++, C, and migration guides in
      [`documentation-plan.md`](documentation-plan.md).

## Deferred Questions

These do not block the initial core work but must be resolved before the
corresponding checklist section is completed:

- Whether malformed moduled input should eventually become globally
  transactional rather than preserving successfully written earlier modules.
- Whether retained LZSS nodes should be converted from source spans to source
  offsets in the first implementation or a later cleanup.
- Whether same-file mapped operations should be rejected or implemented using
  a temporary output file and replacement.
- Whether the C ABI should expose a generic codec identifier in addition to
  per-codec functions.
