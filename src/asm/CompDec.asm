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

; ||||||||||||||| S U B R O U T I N E |||||||||||||||||||||||||||||||||||||||
; ---------------------------------------------------------------------------
CompDec:

.newblock:
	move.w	(a0)+,d0		; fetch description field
	moveq	#15,d3			; set bits counter to 16

.mainloop:
	add.w	d0,d0			; roll description field
	bcs.s	.flag			; if a flag issued, branch
	move.w	(a0)+,(a1)+		; otherwise, do uncompressed data
	dbra	d3,.mainloop	; if bits counter remains, parse the next word
	bra.s	.newblock		; start a new block
; ---------------------------------------------------------------------------
.flag:
	moveq	#-1,d1			; init displacement
	move.b	(a0)+,d1		; load displacement
	add.w	d1,d1
	moveq	#0,d2			; init copy count
	move.b	(a0)+,d2		; load copy length
	beq.s	.end			; if zero, branch
	lea	(a1,d1.w),a2		; load start copy address

.loop:
	move.w	(a2)+,(a1)+		; copy given sequence
	dbra	d2,.loop		; repeat
	dbra	d3,.mainloop	; if bits counter remains, parse the next word
	bra.s	.newblock		; start a new block

.end:
	rts
; ===========================================================================

