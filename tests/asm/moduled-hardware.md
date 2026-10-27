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

The deterministic test shim will map queue/control RAM and the shared buffer separately, keep VRAM in a host-owned 64 KiB image, and emulate only DMA queue insertion/draining. Test-only MMIO commands are an implementation detail of the wrapper; they are not a model of the VDP register interface. DMA events must retain FIFO order, validate source/length/destination, reject buffer reuse before draining, and preserve guards around declared VRAM ranges. Host DMA reads are not counted as CPU bus accesses.

Interrupt testing must inject real MC68000 level-6 exceptions at instruction boundaries, save the same fifteen registers as the expected V-int caller, invoke `Set_Kos_Bookmark`, and return through the real RTE/save/restore code. Include uninterrupted runs, individual interruption positions, repeated interruptions, queue capacity/backpressure, multiple archives/destinations, exact and partial final modules, malformed framing, and the two DMA source-address conventions. Budgets must bound both the CPU and the outer pipeline.

This inventory establishes the minimal test boundary. It does not establish VDP contention, cycle-exact interrupt entry or DMA timing, game-specific interrupt handlers, or real-hardware behavior.
