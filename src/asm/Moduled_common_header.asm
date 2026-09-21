; ---------------------------------------------------------------------------
; Moduled decompression queue header.
;
; Usage pattern:
; 	set module_remap_A000_to_8000,1			; 0 for anything but Kosinski
; 	set module_padding,1					; 0 for anything but Kosinski
; KosMRestoreRegs macro addrW,addrL
; 	movem.w	addrW,d0-d6						; Change this for your word-sized saved register set
; 	movem.l	addrL,a0-a1/a5					; Change this for your longword-sized saved register set
; 	moveq	#(1<<_Kos_LoopUnroll)-1,d7		; Set d7 to the maximum number of loop unrolls.
; 	lea	KosDec_ByteMap(pc),a4				; Load LUT pointer.
; 	endm
; KosMSaveRegs macro addrW,addrL
; 	movem.w	d0-d6,addrW						; Change this for your word-sized saved register set
; 	movem.l	a0-a1/a5,addrL					; Change this for your longword-sized saved register set
; 	; d7 and a4 have fixed, known, values, so no need to save them
; 	endm
; 	include "Moduled_common_header.asm"		; The common header file (this file)
; 	include "Kosinski_internal.asm"			; The internal file for your compression algorithm
; 	include "Moduled_common_footer.asm"		; The common footer file
; ---------------------------------------------------------------------------
; Permission to use, copy, modify, and/or distribute this software for any
; purpose with or without fee is hereby granted.
;
; THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
; WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
; MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
; ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
; WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
; ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT
; OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
; ---------------------------------------------------------------------------
; FUNCTION:
; 	Clear_Kos_Queue
;
; DESCRIPTION
; 	Clears the Kosinski module queue
;
; INPUT:
; 	none
; ---------------------------------------------------------------------------
; FUNCTION:
; 	Queue_Kos_Module
;
; DESCRIPTION
; 	Adds a compressed Moduled archive to the module queue
;
; INPUT:
; 	a1	Source address
; 	d2	Destination address in VRAM
; ---------------------------------------------------------------------------
; FUNCTION:
;	Process_Kos_Module_Queue
;
; DESCRIPTION:
; 	Processes the first module on the queue
; ---------------------------------------------------------------------------
; FUNCTION:
; 	Queue_Kos
;
; DESCRIPTION
; 	Adds Kosinski-compressed data to the decompression queue
;
; INPUT:
; 	a1	Compressed data address
; 	a2	Decompression destination in RAM
; ---------------------------------------------------------------------------
; FUNCTION:
; 	Set_Kos_Bookmark
;
; DESCRIPTION
; 	Checks if V-int occured in the middle of Kosinski queue processing
; 	and stores the location from which processing is to resume if it did
; ---------------------------------------------------------------------------
; FUNCTION:
; 	Process_Kos_Queue
;
; DESCRIPTION
; 	Processes the first entry in the Kosinski decompression queue
; ---------------------------------------------------------------------------

; ---------------------------------------------------------------------------
; Clear the Kosinski module queue.
;
; input:
;  none
; ---------------------------------------------------------------------------
Clear_Kos_Queue:
	moveq	#0,d0
	move.l	d0,(Kos_module_queue).w
	move.w	d0,(Kos_modules_left).w
	move.w	d0,(Kos_decomp_queue_count).w
	rts
; End of function Clear_Kos_Queue
; ===========================================================================

; ---------------------------------------------------------------------------
; Queue a Kosinski Moduled archive for decompression and transfer to VRAM.
;
; The first archive becomes active immediately. Later archives occupy six-byte
; FIFO records (source longword, VRAM destination word). A zero source marks a
; free record. Exceeding the fixed queue capacity enters the error debugger.
;
; input:
;  a1 = address of the archive, including its uncompressed-size header
;  d2.w = destination VRAM byte address
; ---------------------------------------------------------------------------
Queue_Kos_Module:
	lea	(Kos_module_queue).w,a2
	tst.l	(a2)							; is the active record free?
	beq.s	Process_Kos_Module_Queue_Init	; if so, initialize this archive directly
	move.w	#Kos_module_queue_End,d3

.find_free_slot:
	addq.w	#6,a2						; otherwise, inspect the next FIFO record
	cmp.w	a2,d3
	ble.s	.queue_full
	tst.l	(a2)
	bne.s	.find_free_slot

	move.l	a1,(a2)+					; archive address
	move.w	d2,(a2)+					; destination VRAM address
	rts
; ---------------------------------------------------------------------------
.queue_full:
	trap	#15							; prevent corruption beyond the queue
; End of function Queue_Kos_Module
; ===========================================================================

; ---------------------------------------------------------------------------
; Initialize the archive at the head of the module queue.
;
; KosM archives begin with their total uncompressed byte size, then contain
; independently compressed modules of at most $1000 bytes. This routine derives
; the module count and final transfer size, consumes the size header, and sets
; the active archive's initial VRAM destination.
;
; input:
;  a1 = address of the archive's uncompressed-size header
;  d2.w = destination VRAM byte address
; ---------------------------------------------------------------------------
Process_Kos_Module_Queue_Init:
	move.w	(a1)+,d3				; total uncompressed size in bytes
	if module_remap_A000_to_8000<>0
	cmpi.w	#$A000,d3
	bne.s	.size_ready
	move.w	#$8000,d3				; preserve S3&K's special $A000 encoding

.size_ready:
	endif
	lsr.w	#1,d3					; convert total size to words
	move.w	d3,d0
	rol.w	#5,d0
	andi.w	#$1F,d0					; number of complete $800-word modules
	move.w	d0,(Kos_modules_left).w
	andi.w	#$7FF,d3				; final partial module's word count
	bne.s	.last_size_ready
	subq.w	#1,(Kos_modules_left).w	; exact multiple: one full module is also the last
	move.w	#$800,d3

.last_size_ready:
	move.w	d3,(Kos_last_module_size).w
	move.w	d2,(Kos_module_destination).w	; first VRAM destination
	move.l	a1,(Kos_module_source).w		; first module, after the size header
	addq.w	#1,(Kos_modules_left).w			; include the final module
	rts
; End of function Process_Kos_Module_Queue_Init
; ===========================================================================

; ---------------------------------------------------------------------------
; Advance the active KosM archive by one pipeline stage.
;
; A positive Kos_modules_left means the current module has not yet been sent to
; the RAM decompression queue. Bit 15 marks it as queued or decompressed. Once
; the decompression queue becomes empty, the $1000-byte buffer is queued for
; DMA to VRAM, the archive pointers advance, and a completed archive is removed
; from the module FIFO. Normally called after V-int by WaitVInt.
; ---------------------------------------------------------------------------
Process_Kos_Module_Queue:
	tst.w	(Kos_modules_left).w
	bne.s	.module_pending

.return:
	rts
; ---------------------------------------------------------------------------
.module_pending:
	bmi.s	.wait_for_decompression
	cmpi.w	#(Kos_decomp_queue_End-Kos_decomp_queue)/8,(Kos_decomp_queue_count).w
	bhs.s	.return					; wait if the RAM decompression queue is full
	movea.l	(Kos_module_queue).w,a1
	lea	(Kos_decomp_buffer).w,a2
	bsr.w	Queue_Kos				; decompress this module into the shared buffer
	ori.w	#$8000,(Kos_modules_left).w	; mark it as submitted
	rts
; ---------------------------------------------------------------------------
.wait_for_decompression:
	tst.w	(Kos_decomp_queue_count).w
	bne.s	.return					; wait until the decompression pipeline is empty

	andi.w	#$7F,(Kos_modules_left).w	; clear the submitted flag, retaining the count
	move.w	#$800,d3					; full module: $800 words/$1000 bytes
	subq.w	#1,(Kos_modules_left).w
	bne.s	.transfer_size_ready
	move.w	(Kos_last_module_size).w,d3

.transfer_size_ready:
	move.w	(Kos_module_destination).w,d2
	move.w	d2,d0
	add.w	d3,d0
	add.w	d3,d0
	move.w	d0,(Kos_module_destination).w	; advance VRAM by the decompressed byte count
	if module_padding<>0
	move.l	(Kos_module_source).w,d0
	move.l	(Kos_decomp_source).w,d1
	sub.l	d1,d0
	andi.l	#$F,d0
	add.l	d0,d1					; round the consumed compressed stream up to $10 bytes
	move.l	d1,(Kos_module_source).w	; next compressed module
	else
	move.l	(Kos_decomp_source).w,(Kos_module_source).w	; set new source
	endif
	if defined(DMAfunctions_defined) && defined(AssumeSourceAddressInBytes) && ~~AssumeSourceAddressInBytes
	move.l	#dmaSource(Kos_decomp_buffer),d1	; DMA source is expressed in words
	else
	move.l	#Kos_decomp_buffer,d1	; DMA source is expressed in bytes
	endif
	move.w	sr,-(sp)						; protect the DMA queue insertion from V-int
	disableInts
	jsr	(QueueDMATransfer).w
	move.w	(sp)+,sr
	tst.w	(Kos_modules_left).w
	bne.s	.archive_done				; more modules remain in this archive

	; The archive is complete. Remove its record and promote the rest of the FIFO.
	lea	(Kos_module_queue).w,a0
	lea	(Kos_module_queue+6).w,a1
	rept (Kos_module_queue_End-(Kos_module_queue+6))/6
		move.l	(a1)+,(a0)+
		move.w	(a1)+,(a0)+
	endm
	moveq	#0,d0
	move.l	d0,(a0)+				; mark the vacated last record as free
	move.w	d0,(a0)+
	move.l	(Kos_module_queue).w,d0
	beq.s	.archive_done				; no next archive
	movea.l	d0,a1
	move.w	(Kos_module_destination).w,d2
	bra.w	Process_Kos_Module_Queue_Init	; tail-call to activate the promoted record
; ---------------------------------------------------------------------------
.archive_done:
	rts
; End of function Process_Kos_Module_Queue
; ===========================================================================

; ---------------------------------------------------------------------------
; Append one raw Kosinski stream to the RAM decompression FIFO.
;
; The caller is responsible for verifying that a slot is available. Each entry
; is an eight-byte source/destination pointer pair.
;
; input:
;  a1 = compressed data address
;  a2 = decompression destination in RAM
; ---------------------------------------------------------------------------
Queue_Kos:
	move.w	(Kos_decomp_queue_count).w,d0	; existing entry count
	lsl.w	#3,d0						; eight bytes per entry
	lea	(Kos_decomp_queue).w,a3
	move.l	a1,(a3,d0.w)			; compressed source
	move.l	a2,4(a3,d0.w)			; RAM destination
	addq.w	#1,(Kos_decomp_queue_count).w
	rts
; End of function Queue_Kos
; ===========================================================================

; ---------------------------------------------------------------------------
; Divert an interrupted queued decompression through its register-save path.
;
; V-int calls this after saving its own registers. If its exception-frame PC
; lies inside the active decoder body, preserve that PC as the resume bookmark
; and replace it with Backup_Kos_Registers. V-int's eventual RTE will therefore
; save the restored decoder registers and return early to its original caller.
; ---------------------------------------------------------------------------
Set_Kos_Bookmark:
	tst.w	(Kos_decomp_queue_count).w
	bpl.s	.done					; bit 15 clear: no decoder state to preserve
	move.l	$42(sp),d0				; interrupted PC in V-int's exception frame
	cmpi.l	#Process_Kos_Queue.decompress,d0
	blo.s	.done
	cmpi.l	#Process_Kos_Queue.done,d0
	bhs.s	.done
	move.l	$42(sp),(Kos_decomp_bookmark).w	; exact decoder instruction to resume
	move.l	#Backup_Kos_Registers,$42(sp)	; redirect V-int's RTE to the save shim

.done:
	rts
; End of function Set_Kos_Bookmark
; ===========================================================================

; ---------------------------------------------------------------------------
; Process the first queued RAM decompression.
;
; Bit 15 of Kos_decomp_queue_count is an execution flag. A normal entry starts
; the shared Kosinski decoder. If V-int interrupted an earlier invocation, the
; negative count selects Restore_Kos_Bookmark instead. Completed entries are
; removed by shifting the remaining source/destination pairs forward.
; ---------------------------------------------------------------------------
Process_Kos_Queue:
	tst.w	(Kos_decomp_queue_count).w
	beq.w	.done
	bmi.w	Restore_Kos_Bookmark		; resume an invocation interrupted by V-int

.decompress:
	ori.w	#$8000,(Kos_decomp_queue_count).w	; mark decoder execution in progress
	movea.l	(Kos_decomp_source).w,a0
	movea.l	(Kos_decomp_destination).w,a1
