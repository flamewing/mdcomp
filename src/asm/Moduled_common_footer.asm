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
; 	include "Moduled_common_header.asm"		; The common header file
; 	include "Kosinski_internal.asm"			; The internal file for your compression algorithm
; 	include "Moduled_common_footer.asm"		; The common footer file (this file)
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
	move.l	a0,(Kos_decomp_source).w
	move.l	a1,(Kos_decomp_destination).w
	andi.w	#$7FFF,(Kos_decomp_queue_count).w	; clear decompression in progress bit
	subq.w	#1,(Kos_decomp_queue_count).w
	beq.s	.Done								; branch if there aren't any entries remaining in the queue
	lea	(Kos_decomp_queue).w,a0
	lea	(Kos_decomp_queue+8).w,a1				; otherwise, shift all entries up
	rept (Kos_decomp_queue_End-(Kos_decomp_queue+8))/4
		move.l	(a1)+,(a0)+
	endm

.Done:
	rts
; ---------------------------------------------------------------------------
Restore_Kos_Bookmark:
	KosMRestoreRegs (Kos_decomp_stored_Wregisters).w,(Kos_decomp_stored_Lregisters).w
	move.l	(Kos_decomp_bookmark).w,-(sp)
	move.w	(Kos_decomp_stored_SR).w,-(sp)
	rte
; End of function Process_Kos_Queue
; ===========================================================================

; ||||||||||||||| S U B R O U T I N E |||||||||||||||||||||||||||||||||||||||
; ---------------------------------------------------------------------------
; Backs up current state for later restoration.
; ---------------------------------------------------------------------------
Backup_Kos_Registers:
	move	sr,(Kos_decomp_stored_SR).w
	KosMSaveRegs (Kos_decomp_stored_Wregisters).w,(Kos_decomp_stored_Lregisters).w
	rts
; ===========================================================================
