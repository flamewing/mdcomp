; -----------------------------------------------------------------------------
; Comper-X a newer, much faster implementation of Comper compression
;
; (c) 2021, vladikcomper
; -----------------------------------------------------------------------------
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
; -----------------------------------------------------------------------------
; INPUT:
;		a0		- Source Offset
;		a1		- Destination Offset
;
; USES:
;		d0-d4, a2
; -----------------------------------------------------------------------------

ComperXDec:
	moveq	#-1, d1					; d1 is used for negative sign-extended displacement
	moveq	#0, d2					; d2 is used as 8-bit index for copy jump tables

.load_flags_field:
	moveq	#16-1, d3				; d3 = description field bits counter
	move.w	(a0)+, d0				; d0 = description field data

.fetch_flag:
	add.w	d0, d0					; roll description field
	bcs.s	.flag					; if a flag issued, branch
	move.w	(a0)+, (a1)+ 			; otherwise, do uncompressed data

.flag_next:
	dbf 	d3, .fetch_flag			; if bits counter remains, parse the next word
	bra.s	.load_flags_field		; start a new block
; -----------------------------------------------------------------------------
.flag:
	move.b	(a0)+, d1				; d1 = Displacement (words) (sign-extended)
	beq.w	.copy_rle				; displacement value of 0 (-1) triggers RLE mode
	move.b	(a0)+, d2				; d2 = Copy length field

	add.w	d1, d1					; d1 = Displacement * 2 (sign-extended)
	lea 	-2(a1,d1.w), a2			; a2 = Start copy address

	moveq	#-1, d1					; restore the value of d1 now ...
	add.b	d2, d2					; test MSB of copy length field ...
	bcc.s	.copy_long_start		; if not set, then transfer is even words, branch ...
	move.w	(a2)+, (a1)+			; otherwise, copy odd word before falling into longwords loop ...

.copy_long_start:
	; d2 = 0..$FE
	jmp		.CopyDevice(pc,d2.w)
; -----------------------------------------------------------------------------
; Copy device for non-RLE transfers
; -----------------------------------------------------------------------------

.CopyDevice:
	; copy length = 0 stops decompression
	if comperx_return_reg<>0
	jmp	(comperx_return_reg)
	else
	rts
	endif
; -----------------------------------------------------------------------------
	rept 127
		move.l	(a2)+, (a1)+
	endm
	dbf 	d3, .fetch_flag			; if bits counter remains, parse the next word
	bra.w	.load_flags_field
; =============================================================================

; -----------------------------------------------------------------------------
; Copy device for RLE transfers
; -----------------------------------------------------------------------------

	; copy length = 0 stops decompression
	if comperx_return_reg<>0
	jmp	(comperx_return_reg)
	else
	rts
	endif
; -----------------------------------------------------------------------------
	rept 127
		move.l	d4, (a1)+
	endm
.CopyDevice_RLE:
	dbf d3, .fetch_flag				; if bits counter remains, parse the next word
	bra.w	.load_flags_field
; -----------------------------------------------------------------------------
.copy_rle:
	move.b	(a0)+, d1				; d1 = - $100 + Copy length

	move.w	-(a1), d4
	swap	d4
	move.w	(a1)+, d4				; d4 = data to copy

	add.b	d1, d1					; test MSB of copy length field ...
	bcc.s	.copy_long_rle_start	; if not set, then transfer is even words, branch ...
	move.w	d4, (a1)+				; otherwise, copy odd word before falling into longwords loop ...

.copy_long_rle_start:
	; d1 = -$100..-2
	jmp		.CopyDevice_RLE(pc,d1.w)
; =============================================================================
