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
	set module_remap_A000_to_8000 0			; 0 for anything but Kosinski
	set module_padding 0					; 0 for anything but Kosinski

	reg comperx_return_reg,a5				; Return here at the end of the ComperX decompression.

KosMRestoreRegs macro addrW,addrL
	movem.w	addrW,d0-d4
	movem.l	addrL,a0-a2
	lea ComperX_done(pc),comperx_return_reg	; Set comperx_return_reg to the end of the ComperX code
	endm

KosMSaveRegs macro addrW,addrL
	movem.w	d0-d4,addrW
	movem.l	a0-a2,addrL
	; comperx_return_reg has a fixed, known, values, so no need to save it
	endm

	include "Moduled_common_header.asm"		; The common header file (this file)
	comperx_jump_to_reg_on_end,1				; Jump to (comperx_return_reg) on end
	lea ComperX_done(pc),comperx_return_reg	; Set comperx_return_reg to the end of the ComperX code
	include "ComperX_internal.asm"			; The internal file for your compression algorithm
ComperX_done:
	include "Moduled_common_footer.asm"		; The common footer file
