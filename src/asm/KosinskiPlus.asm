; ---------------------------------------------------------------------------
; New format based on Kosinski. It changes several design decisions to allow
; a faster decompressor without loss of compression ratio.
; Created originally by Flamewing and vladikcomper (by discussions on IRC),
; further improvements by Clownacy.
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
; 	KosPlusDec
;
; DESCRIPTION
; 	Kosinski+ Decompressor
;
; INPUT:
; 	a0	Source address
; 	a1	Destination address
; ---------------------------------------------------------------------------
_KosPlus_LoopUnroll = 3

_KosPlus_ReadBit macro
	dbra	d2,.skip
	moveq	#7,d2						; We have 8 new bits, but will use one up below.
	move.b	(a0)+,d0					; Get desc field low-byte.
.skip:
	add.b	d0,d0						; Get a bit from the bitstream.
	endm
; ===========================================================================

; ||||||||||||||| S U B R O U T I N E |||||||||||||||||||||||||||||||||||||||
; ---------------------------------------------------------------------------
KosPlusDec:
	include "KosinskiPlus_internal.asm"			; The internal file for your compression algorithm
	rts
; ===========================================================================

