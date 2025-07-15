; ---------------------------------------------------------------------------
; Moduled Comper decompression queue.
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
	set module_remap_A000_to_8000,0			; 0 for anything but Kosinski
	set module_padding,0					; 0 for anything but Kosinski

KosMRestoreRegs macro addrW,addrL
	movem.w	addrW,d0/d2/d3-d6
	movem.l	addrL,a0-a1/a5
	moveq	#(1<<_KosPlus_LoopUnroll)-1,d7	; Set d7 to the maximum number of loop unrolls.
	lea	KosDec_ByteMap(pc),a4				; Load LUT pointer.
	endm

KosMSaveRegs macro addrW,addrL
	movem.w	d0/d2/d3-d6,addrW
	movem.l	a0-a1/a5,addrL
	; d7 and a4 have fixed, known, values, so no need to save them
	endm

	include "Moduled_common_header.asm"		; The common header file (this file)
	include "KosinskiPlus_internal.asm"		; The internal file for your compression algorithm
	include "Moduled_common_footer.asm"		; The common footer file
