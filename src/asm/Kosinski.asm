; ---------------------------------------------------------------------------
; For format explanation see https://segaretro.org/Kosinski_compression
; New faster version written by vladikcomper, with additional improvements
; by MarkeyJester and Flamewing
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
; 	KosDec
;
; DESCRIPTION
; 	Kosinski Decompressor
;
; INPUT:
; 	a0	source address
; 	a1	destination address
; ---------------------------------------------------------------------------
_Kos_UseLUT = 1
_Kos_LoopUnroll = 3
_Kos_ExtremeUnrolling = 1

_Kos_RunBitStream macro
	dbra	d2,.skip
	moveq	#7,d2					; We have 8 new bits, but will use one up below.
	move.b	d1,d0					; Use the remaining 8 bits.
	not.w	d3						; Have all 16 bits been used up?
	bne.s	.skip					; Branch if not.
	move.b	(a0)+,d0				; Get desc field low-byte.
	move.b	(a0)+,d1				; Get desc field hi-byte.
	if _Kos_UseLUT==1
		move.b	(a4,d0.w),d0		; Invert bit order...
		move.b	(a4,d1.w),d1		; ... for both bytes.
	endif
.skip
	endm

_Kos_ReadBit macro
	if _Kos_UseLUT==1
		add.b	d0,d0				; Get a bit from the bitstream.
	else
		lsr.b	#1,d0				; Get a bit from the bitstream.
	endif
	endm
; ===========================================================================

; ||||||||||||||| S U B R O U T I N E |||||||||||||||||||||||||||||||||||||||
; ---------------------------------------------------------------------------
KosDec:
	include "_inc/Kosinski_internal.asm"
	rts								; End of function KosDec.
; ===========================================================================
	if _Kos_UseLUT==1
KosDec_ByteMap:
	set .c,0
	rept 256
		dc.b .c><8
		set .c,.c+1
	endm
	endif
; ===========================================================================

