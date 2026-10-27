# Moduled decoder hardware contract

The four shipped queues (`Comper_Moduled.asm`, `ComperX_Moduled.asm`, `Kosinki_Moduled.asm`, and `KosinkiPlus_Moduled.asm`) include the shared header/footer. Their hardware boundary is the external `QueueDMATransfer` routine and the caller's V-int flow. The decoder instructions themselves do not write VDP registers. Testing this boundary does not require emulating the full Mega Drive.

| Dependency | Contract to exercise |
| --- | --- |
| Module FIFO | Six-byte records: source longword and VRAM byte destination word. The active source/destination symbols alias the first record. Queue overflow traps rather than writing past the records. |
| Raw decompression FIFO | Eight-byte source/destination pointer pairs; bit 15 of its count marks execution/resume. Completed entries shift forward. |
| Shared RAM buffer | One 4096-byte buffer reused between modules. Its contents must remain intact until the queued DMA reads them. Match history must not leak between modules. |
| DMA insertion | D1 is the source, D2.w the VRAM byte destination, and D3.w the transfer length in words. D1 is a byte address normally, or a word address when the integration defines the documented DMA helper macros. |
| Interrupt exclusion | DMA insertion saves SR, executes the supplied `disableInts` macro, calls the queue routine, then restores SR. The insertion routine must observe interrupts masked. |
| V-int exception frame | The bookmark helper expects the interrupted PC at `$42(sp)`: 60 bytes of saved D0–A6 registers, a four-byte JSR return address, and the two-byte exception SR precede that PC. |
| Bookmark/save/restore | Only PCs in the active decoder body are redirected. V-int restores its registers and executes RTE into the backup shim; a later queue call restores decoder registers/SR and executes RTE to the exact bookmarked instruction. |
| Module boundaries | Modules are at most 4096 decompressed bytes. The final DMA uses the header-derived word count. Kosinski aligns compressed modules to 16-byte boundaries and retains the historical `$A000` size-header interpretation as `$8000`. |
| Frame scheduling | Drain previously queued DMA before allowing the next decompression to overwrite the shared buffer. Flush the final transfer before declaring the archive pipeline complete. |

The deterministic test shim maps queue/control RAM and the shared buffer separately, keeps VRAM in a host-owned 64 KiB image, and emulates only DMA queue insertion/draining. Test-only MMIO commands are an implementation detail of the wrapper; they are not a model of the VDP register interface. DMA events retain FIFO order, validate source/length/destination, reject buffer reuse before draining, and preserve guards around declared VRAM ranges. Host DMA reads are not counted as CPU bus accesses.

Interrupt testing must inject real MC68000 level-6 exceptions at instruction boundaries, save the same fifteen registers as the expected V-int caller, invoke `Set_Kos_Bookmark`, and return through the real RTE/save/restore code. Include uninterrupted runs, individual interruption positions, repeated interruptions, queue capacity/backpressure, multiple archives/destinations, exact and partial final modules, malformed framing, and the two DMA source-address conventions. Budgets must bound both the CPU and the outer pipeline.

This inventory establishes the minimal test boundary. It does not establish VDP contention, cycle-exact interrupt entry or DMA timing, game-specific interrupt handlers, or real-hardware behavior.

## Implemented qualification

Eight images cover all four queues with byte- and word-address DMA source ABIs. The recorded [module audit](../references/module-audit.json) contains 1,144 checks, including 2,392 level-6 exceptions and 1,438 completed transfers. Bootstrap constructs module frames from independent literals and long/RLE matches, then interrupts every distinct decoder PC reached by those streams. Additional cases interrupt outside the decoder, repeat interruptions, fill the raw FIFO before submission, promote four different archives/destinations, reject a fifth queued archive, wrap VRAM, skip empty archives, and exercise exact/partial module sizes through 32 KiB. Kosinski's `$A000`/`$8000` alias has an explicit case. A delayed-drain negative case detects premature buffer reuse; a mismatched size header is contained by the DMA extent check. Bootstrap gates C++ encoder interoperability through CTest fixtures.

The wrapper exports `MDM6`, the normal entry/payload/sentinel, V-int entry, decoder range, and DMA ABI. Queue/control RAM occupies `$FF8000..$FF80FF`, the guarded module buffer `$FF9000..$FF9FFF`, auxiliary raw-queue destinations `$FFA000..$FFAFFF`, and the existing separate guarded CPU stack is retained. The test device at `$E00000` accepts source, destination, length, enqueue, and drain commands; it is deliberately outside the VDP address space. Host drains clear the buffer's written-state tracking only after a transfer, preventing history leakage between modules. Instruction, cycle, frame, and CTest budgets bound failures.

Two additional TSV files, `module-bootstrap.tsv` and `module-encode.tsv`, record image name, payload FNV-1a digest, archive count, scheduling flags, interrupt position/period, outside-decoder and trace flags, expected/actual completion, VRAM equality, transfer/interrupt/bookmark counts, instruction/cycle totals, and pass status. The digest identifies synthetic inputs rather than providing a cryptographic provenance claim. `record_module_audit.py` validates successful reports and records source/image SHA-256 hashes and exports. Unexpected failures save packed input, full expected/actual VRAM images, and machine diagnostics; console comparisons remain bounded.

## Defects exposed

- Comper and ComperX module settings omitted required commas in `set` directives. These now assemble with the pinned ASL.
- The queue-end immediate needs its low 16 bits when queue symbols use sign-extended RAM addresses. Masking the immediate preserves the intended comparison.
- The shared footer's local `.done` label acquired ComperX's surrounding global scope, leaving `Process_Kos_Queue.done` undefined. The footer now defines and branches to that explicit label.
- Kosinski Plus restore code referenced a nonexistent Kosinski LUT. Its decoder has no such LUT; the unused LEA was removed.
- ComperX stored D4 as a word although its RLE path uses a full longword pattern. It now saves D0–D3 as words and D4/A0–A2 as longwords. Integrators must reserve **8 bytes of word-register storage and 16 bytes of long-register storage**, replacing the previous 10/12-byte split. Independent RLE interruption fixtures reproduce and guard the corruption.
- A zero-size archive left an occupied active FIFO record with no modules to process. Queue insertion now skips empty archives before consuming a slot, allowing later nonempty archives to proceed.

The large binary mismatch initially exposed a separate [Catch2 reporter failure](../references/catch2-reporter/README.md). Bounded equality diagnostics and a Windows error-mode guard prevent that path from producing another application-error dialog during normal test failures.
