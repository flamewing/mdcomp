; ---------------------------------------------------------------------------
; For format explanation see https://segaretro.org/Saxman_compression
; ---------------------------------------------------------------------------
; FUNCTION:
; 	SaxDec
;
; DESCRIPTION
; 	Saxman Decompressor. Assumes file length is at the start as a
; 	little-endian word. In S2, this means music.
;
; INPUT:
; 	a0	Source address
; 	a1	Destination address
; ---------------------------------------------------------------------------
; FUNCTION:
; 	SaxDec2
; DESCRIPTION
; 	Saxman Decompressor. Assumes file length is given by d6.
; 	In S2, this means the sound driver.
;
; INPUT:
; 	a0	Source address
; 	a1	Destination address
; 	d6	Length of compressed file
; ---------------------------------------------------------------------------
_Sax_UseLUT = 1

; Assume source data is aligned in an even address.
_Sax_AlignedSource = 1

_SaxDec_ReadByte macro dst
	if "dst"<>""
		move.b	(a0)+,dst
		shift
		_SaxDec_ReadByte ALLARGS
	endif
	endm

_SaxDec_GetByte macro dst
  if ARGCOUNT>0
	if ARGCOUNT>8
		subi.w	#ARGCOUNT,d6		; decrement remaining number of bytes
	else
		subq.w	#ARGCOUNT,d6		; decrement remaining number of bytes
	endif
	if ARGCOUNT>1
		bhi.s	.continue
	else
		bne.s	.continue
	endif
	rts								; exit the decompressor by meddling with the stack
.continue:
	_SaxDec_ReadByte ALLARGS
  endif
	endm

_Sax_ReadBit macro
	dbra	d2,.skip
	moveq	#7,d2					; we have 8 new bits, but will use one up below.
	_SaxDec_GetByte d0				; get desc field low-byte.
	if _Sax_UseLUT==1
		move.b	(a2,d0.w),d0			; invert bit order.
	endif
.skip
	if _Sax_UseLUT==1
		add.b	d0,d0					; get a bit from the bitstream.
	else
		lsr.b	#1,d0					; get a bit from the bitstream.
	endif
	endm
; ===========================================================================

; ||||||||||||||| S U B R O U T I N E |||||||||||||||||||||||||||||||||||||||
; ---------------------------------------------------------------------------
SaxDec:
	; Read and byte-swap one word.
	if _Sax_AlignedSource==1
		move.w	(a0)+,d6
		rol.w	#8,d6
	else
		move.b	(a0)+,d0
		move.b	(a0)+,-(sp)
		move.w	(sp)+,d6
		move.b	d0,d6
	endif
	; FALL THROUGH

; ||||||||||||||| S U B R O U T I N E |||||||||||||||||||||||||||||||||||||||
; ---------------------------------------------------------------------------
SaxDec2:
	moveq	#0,d2					; Flag as having no bits left.
	lea	(a1),a4
	if _Sax_UseLUT==1
		moveq	#0,d0
		lea	SaxDec_ByteMap(pc),a2		; Load LUT pointer.
	endif
	move.w	#$F000,d3
	moveq	#$F,d7					; Unrolling mask

.loop:
	_Sax_ReadBit
	bcc.s	.read_compressed
	; Uncompressed byte
	_SaxDec_GetByte (a1)+
	bra.s	.loop
; ---------------------------------------------------------------------------
.copy:
	rept 18
		move.b	(a5)+,(a1)+
	endm
	bra.s	.loop
; ---------------------------------------------------------------------------
.fill0:
	rept 18
		move.b	d3,(a1)+			; Exploiting the fact that low byte of d3 is zero
	endm
	bra.s	.loop
; ---------------------------------------------------------------------------
.read_compressed:
	_SaxDec_GetByte d1, d5
	move.b	d5,d4					; Low nibble of d4 is the length of the match minus 3
	not.w	d4						; Flip bits for jump table
	and.w	d7,d4					; Want only the low nibble
	add.w	d4,d4					; Jump table is in words
	lsl.w	#4,d5					; Move high nibble into place
	move.b	d1,d5					; Put low byte in place
	addi.w	#$12,d5					; There is a weird $12 byte shift in Saxman format
	; Rebase offset from its storage format to a more useful one.
	sub.w	a1,d5
	add.w	a4,d5					; d5 -= number of bytes decompressed so far
	or.w	d3,d5					; d5 = (d5 & $FFF) - $1000
	lea	(a1,d5.w),a5
	cmpa.l	a4,a5					; Is rebased offset is before start?
	blo.s	.zero_fill				; Branch if yes
	jmp	.copy(pc,d4.w)
; ---------------------------------------------------------------------------
.zero_fill:
	jmp	.fill0(pc,d4.w)
; End of function SaxDec
; ===========================================================================
	if _Sax_UseLUT==1
SaxDec_ByteMap:
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


