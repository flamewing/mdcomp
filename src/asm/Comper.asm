; ---------------------------------------------------------------------------
; Original version written by vladikcomper, with improvements by Flamewing
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
; 	CompDec
;
; DESCRIPTION
; 	Comper Decompressor
;
; INPUT:
; 	a0	Source address
; 	a1	Destination address
; ---------------------------------------------------------------------------
	if defined(_Comp_LoopUnroll)==0
_Comp_LoopUnroll = 3
	endif

_Comp_RunBitStream macro
	dbra	d3,.mainloop	; if bits counter remains, parse the next word
	bra.ATTRIBUTE	.newblock		; start a new block
	endm

_Comp_ReadBit macro
	add.w	d0,d0			; roll description field
	endm
; ===========================================================================


; ||||||||||||||| S U B R O U T I N E |||||||||||||||||||||||||||||||||||||||
; ---------------------------------------------------------------------------
CompDec:
	include "Comper_internal.asm"			; The internal file for your compression algorithm
	rts
; ===========================================================================
