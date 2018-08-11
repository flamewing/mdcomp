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
; 	RocketDec
;
; DESCRIPTION
; 	Rocket Decompressor. Actually based on my Saxman decompressor, instead
; 	of the original Rocket Knight Adventures decompressor.
;
; INPUT:
; 	a0	Source address
; 	a1	Destination address
; ---------------------------------------------------------------------------
_Rocket_LoopUnroll = 3

_Rocket_UseLUT = 1

_Rocket_ReadBit macro
	dbra	d2,.skip
	moveq	#7,d2						; we have 8 new bits, but will use one up below.
	move.b	(a0)+,d0					; get desc field low-byte.
	if _Rocket_UseLUT==1
		move.b	(a2,d0.w),d0			; invert bit order.
	endif
.skip
	if _Rocket_UseLUT==1
		add.b	d0,d0					; get a bit from the bitstream.
	else
		lsr.b	#1,d0					; get a bit from the bitstream.
	endif
	endm
; ===========================================================================

; ||||||||||||||| S U B R O U T I N E |||||||||||||||||||||||||||||||||||||||
; ---------------------------------------------------------------------------
RocketDec:
	move.w	(a0)+,d0				; Read uncompressed file length, and discard it
	move.w	(a0)+,d0				; Read compressed data length
	lea	(a0,d0.w),a3				; End of compression pointer
	move.w	#-$3C0,d0				; Position bias
	lea	(a1),a4						; Copy of start position
	lea	(a1,d1.w),a6				; Apply also position bias
	moveq	#$20,d1
	move.w	#$FC00,d3
	if _Rocket_UseLUT==1
		moveq	#0,d0
		lea	RocketDec_ByteMap(pc),a2		; Load LUT pointer.
	endif
	moveq	#0,d2					; Flag as having no bits left.
	moveq	#(1<<_Rocket_LoopUnroll)-1,d7

.loop:
	cmpa.l	a3,a0					; Are we at the end of the compressed data?
	bcc.s	.end					; If so, we're finished; end decompression loop
	_Rocket_ReadBit
	bcc.s	.read_compressed
	; Uncompressed byte
	move.b	(a0)+,(a1)+
	bra.s	.loop
; ---------------------------------------------------------------------------
.end:
	rts
; ---------------------------------------------------------------------------
.fill20:
	rept (1<<_Rocket_LoopUnroll)
		move.b	d1,(a1)+
	endm
	dbra	d5,.fill20
	tst.w	d4						; Any copies left?
	bmi.s	.loop					; Branch if not

.just_copy:
	move.w	d4,d6
	not.w	d6
	and.w	d7,d6
	add.w	d6,d6
	lsr.w	#_Rocket_LoopUnroll,d4
	jmp	.copy(pc,d6.w)
.copy:
	rept (1<<_Rocket_LoopUnroll)
		move.b	(a5)+,(a1)+
	endm
	dbra	d4,.copy
	bra.s	.loop
; End of function RocketDec
; ---------------------------------------------------------------------------
.read_compressed:
	moveq	#0,d4
	move.b	(a0)+,d4				; d4 = %ccccccdd
	ror.b	#2,d4					; d4 = %ddcccccc
	move.w	d4,d5					; d5 = %ddcccccc
	andi.w	#$3F,d4					; d4 is now copy count-1
	add.w	d5,d5					; d5 = %ddcccccc0
	add.w	d5,d5					; d5 = %ddcccccc00
	move.b	(a0)+,d5				; d5 is now base offset
	; Rebase offset
	sub.w	a6,d5
	add.w	a4,d5					; d5 -= number of bytes decompressed - position bias
	or.w	d3,d5					; d5 = (d5 & $3FF) - $400
	lea	(a1,d5.w),a5
	move.l	a5,d5
	sub.l	a4,d5					; Is rebased offset is before start?
	bhs.s	.just_copy				; Branch if not
	; Need to copy some spaces. -d5 is how many.
	add.w	d5,d4					; d4 is number of copies-1 to do after
	sub.w	d5,a5					; Seek source of copy by as many 0x20ś as we will write
	not.w	d5						; d5 = -d5-1, a loop index
	move.w	d5,d6
	not.w	d6
	and.w	d7,d6
	add.w	d6,d6
	lsr.w	#_Rocket_LoopUnroll,d5
	jmp	.fill20(pc,d6.w)
; ===========================================================================
	if _Rocket_UseLUT==1
RocketDec_ByteMap:
	dc.b	$00,$80,$40,$C0,$20,$A0,$60,$E0,$10,$90,$50,$D0,$30,$B0,$70,$F0
	dc.b	$08,$88,$48,$C8,$28,$A8,$68,$E8,$18,$98,$58,$D8,$38,$B8,$78,$F8
	dc.b	$04,$84,$44,$C4,$24,$A4,$64,$E4,$14,$94,$54,$D4,$34,$B4,$74,$F4
	dc.b	$0C,$8C,$4C,$CC,$2C,$AC,$6C,$EC,$1C,$9C,$5C,$DC,$3C,$BC,$7C,$FC
	dc.b	$02,$82,$42,$C2,$22,$A2,$62,$E2,$12,$92,$52,$D2,$32,$B2,$72,$F2
	dc.b	$0A,$8A,$4A,$CA,$2A,$AA,$6A,$EA,$1A,$9A,$5A,$DA,$3A,$BA,$7A,$FA
	dc.b	$06,$86,$46,$C6,$26,$A6,$66,$E6,$16,$96,$56,$D6,$36,$B6,$76,$F6
	dc.b	$0E,$8E,$4E,$CE,$2E,$AE,$6E,$EE,$1E,$9E,$5E,$DE,$3E,$BE,$7E,$FE
	dc.b	$01,$81,$41,$C1,$21,$A1,$61,$E1,$11,$91,$51,$D1,$31,$B1,$71,$F1
	dc.b	$09,$89,$49,$C9,$29,$A9,$69,$E9,$19,$99,$59,$D9,$39,$B9,$79,$F9
	dc.b	$05,$85,$45,$C5,$25,$A5,$65,$E5,$15,$95,$55,$D5,$35,$B5,$75,$F5
	dc.b	$0D,$8D,$4D,$CD,$2D,$AD,$6D,$ED,$1D,$9D,$5D,$DD,$3D,$BD,$7D,$FD
	dc.b	$03,$83,$43,$C3,$23,$A3,$63,$E3,$13,$93,$53,$D3,$33,$B3,$73,$F3
	dc.b	$0B,$8B,$4B,$CB,$2B,$AB,$6B,$EB,$1B,$9B,$5B,$DB,$3B,$BB,$7B,$FB
	dc.b	$07,$87,$47,$C7,$27,$A7,$67,$E7,$17,$97,$57,$D7,$37,$B7,$77,$F7
	dc.b	$0F,$8F,$4F,$CF,$2F,$AF,$6F,$EF,$1F,$9F,$5F,$DF,$3F,$BF,$7F,$FF
	endif
; ===========================================================================


