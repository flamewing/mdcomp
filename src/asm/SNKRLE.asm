; ---------------------------------------------------------------------------
; Original by snkenjoi, this much faster version written by Flamewing
; ---------------------------------------------------------------------------
; FUNCTION:
; 	SNKDec
;
; DESCRIPTION
; 	snkenjoi's RLE Decompressor
;
; INPUT:
; 	a0	Source address
; 	d2	VRAM Destination address
; ===========================================================================


; ||||||||||||||| S U B R O U T I N E |||||||||||||||||||||||||||||||||||||||
; ---------------------------------------------------------------------------
SNKDec:
	move.w	#$2700,sr						; 16(2/0)
	movem.l	d0/d1/d3-d6/a2-a3,-(sp)			; 72(2/16)

SNKDecToVRAM:
	VRAMCommReg d2, WRITE, 1				; 38(6/0)
	lea	(VDP_data_port).l,a2				; 12(3/0)
	move.l	d2,VDP_control_port-VDP_data_port(a2)	; 16(2/2)

SNKDecMain:
	;16 words = 1 tile
	; Allocate some space on stack
	subq.w	#2,sp							;  8(1/0)
	move.w	#$FF,d0							;  8(2/0)
	moveq	#0,d1							;  4(1/0)
	move.w	(a0)+,d1						;  8(2/0)
	lsl.l	#5,d1							; 16(1/0)
	lea	SNKDec_CopyLUT_End(pc),a3			;  8(2/0)

.fetch_new_char:
	; PRECONDITIONS:
	; (0)	The buffer in d6 is empty.
	; Read a byte from input stream.
	move.b	(a0)+,d3						;  8(2/0)

.print_to_buffer:
	; Print the character on d3 to the temporary buffer (d6).
	; Using a RAM mini-buffer is faster by 10(-1/0) than using lsl.
	move.b	d3,(sp)							;  8(2/0)
	move.w	(sp),d6							;  8(2/0)

.main_loop:
	; PRECONDITIONS:
	; (1)	there is an even number of bytes left to decompress;
	; (2)	last character read is in d3;
	; (3)	d3 was printed to the buffer (d6);
	; (4)	we are at an even position on the output stream.
	; Read and print new character to the buffer.
	move.b	(a0)+,d6						;  8(2/0)
	; We now have a word on the buffer. Write it to output stream.
	move.w	d6,(a2)							;  8(1/1)
	subq.w	#2,d1							;  4(1/0)
	; PRECONDITIONS: (1), (2) and (4) still hold; (3) is replaced by:
	; (3')	the buffer (d6) is empty.
	; Branch if decompression was finished.
	beq.s	SNKDecEnd						; T: 10(2/0); N:  8(1/0)
	; Is this a run of identical bytes?
	cmp.b	d3,d6							;  4(1/0)
	; Branch if not.
	bne.s	.main_loop_buffer_clear			; T: 10(2/0); N:  8(1/0)
	; We have a run. Fetch number of repetitions.
	move.b	(a0)+,d5						;  8(2/0)
	; Zero repetitions means we wrote what was required and emptied our buffer.
	beq.s	.main_loop_buffer_clear			; T: 10(2/0); N:  8(1/0)
	; PRECONDITIONS: (1), (2), (3') and (4) still hold;
	; We have one more precondition:
	; (5)	the two bytes of the low word of d6 are equal to d3.
	; Given all preconditions, we just have to write d6 several times.
	; Strip off junk in high bits of repetition count.
	and.w	d0,d5							;  4(1/0)
	if 1==1
		; TODO: do we even need this? This would point to an unreliable
		; compressor which should be fixed...
		; Do we have a copy count higher than the number of characters remaining?
		cmp.w	d1,d5						;  4(1/0)
		; Branch if not.
		bls.s	.got_count1					; T: 10(2/0); N:  8(1/0)
		; Cap count to number of bytes remaining.
		move.w	d1,d5						;  4(1/0)

.got_count1:
	endif
	; Save count for later.
	move.w	d5,d4							;  4(1/0)
	; Strip off low bit, as we will write words.
	andi.b	#$FE,d5							;  8(2/0)
	; Prepare for loop unroll.
	neg.w	d5								;  4(1/0)
	; Copy with lookup table.
	jsr	(a3,d5.w)							; 22(2/2)
	; NOTE: because of the 5 preconditions above, then we have the following:
	; (A)	if the copy count is odd, we haven't finished the decompression.
	; Therefore, if we have reached the end of file, we don't have to worry
	; about the low bit of the copy count we saved above.
	; Factor in all bytes we wrote.
	sub.w	d4,d1							;  4(1/0)
	; Branch if decompression is over.
	beq.s	SNKDecEnd						; T: 10(2/0); N:  8(1/0)
	; Was the copy count an odd number?
	moveq	#1,d5							;  4(1/0)
	and.w	d4,d5							;  4(1/0)
	; Branch if not.
	beq.s	.no_single_byte					; T: 10(2/0); N:  8(1/0)
	; PRECONDITIONS: all of (2), (3'), (4) and (5) still hold. Instead of (1),
	; we have:
	; (1')	there is an odd number of bytes left to decompress.
	; Implicitly print byte at d3 to buffer at d6 by doing (almost) nothing.
	; Add a byte back to count because we haven't put it into the output stream.
	addq.w	#1,d1							;  4(1/0)
	; PRECONDITIONS: all of (1), (2), (3), (4) and (5) hold.
	; Was the copy count 255?
	addq.b	#1,d4							;  4(1/0)
	; Branch if not.
	bne.s	.main_loop						; T: 10(2/0); N:  8(1/0)
	; We need to read in a new character, and we have one printed to the buffer.
	; Read and print new character to the buffer.
	move.b	(a0)+,d6						;  8(2/0)
	; Write both to output stream.
	move.w	d6,(a2)							;  8(1/1)
	subq.w	#2,d1							;  4(1/0)
	; Branch if decompression was finished.
	beq.s	SNKDecEnd						; T: 10(2/0); N:  8(1/0)
	bra.s	.main_loop_buffer_clear			; 10(2/0)
;----------------------------------------------------------------------------
.no_single_byte:
	; PRECONDITIONS: all of (1), (2), (3'), (4) and (5) still hold.
	; If copy count is 255, we need to fetch a new byte and write it to the
	; buffer.
	; Was the copy count 255?
	addq.b	#1,d4							;  4(1/0)
	; Branch if yes.
	beq.s	.fetch_new_char					; T: 10(2/0); N:  8(1/0)

.main_loop_buffer_clear:
	; PRECONDITIONS:
	; (1)	there is an even number of bytes left to decompress;
	; (2")	last character read is in d6;
	; (3")	the buffer (d3) is empty;
	; (4)	we are at an even position on the output stream.
	; Read and print new character to the buffer.
	move.b	(a0)+,d3						;  8(2/0)
	; Is this a run of identical bytes?
	cmp.b	d3,d6							;  4(1/0)
	; Branch if not.
	bne.s	.print_to_buffer				; T: 10(2/0); N:  8(1/0)
	; We have a run. Fetch number of repetitions.
	move.b	(a0)+,d5						;  8(2/0)
	; Zero repetitions means we just need to print the character to the buffer.
	beq.s	.print_to_buffer				; T: 10(2/0); N:  8(1/0)
	; Strip off junk in high bits of repetition count.
	and.w	d0,d5							;  4(1/0)
	; PRECONDITIONS: all of (1), (2), (3'), (4) and (5) still hold.
	; NOTE: preconditions (1), (2"), (3") and (4) are maintained by the above
	; operations. Since the bytes at d3 and d6 are equal, we will shift to d6 as
	; buffer and d3 as last character read. This swaps precondition (2") by (2)
	; and precondition (3") by
	; (3')	the buffer (d6) is empty;
	; Print the character on d3 to the temporary buffer (d6).
	; Using a RAM mini-buffer is faster by 10(-1/0) than using lsl.
	move.b	d3,(sp)							;  8(2/0)
	move.w	(sp),d6							;  8(2/0)
	move.b	d3,d6							;  4(1/0)
	; We now have a word on the buffer. Write it to output stream.
	move.w	d6,(a2)							;  8(1/1)
	subq.w	#2,d1							;  4(1/0)
	; PRECONDITIONS: (1), (2) and (4) still hold; (3) is replaced by:
	; (3')	the buffer (d6) is empty.
	; Deduct one byte from the copy count since we printed it above.
	subq.w	#1,d5
	; Save count for later.
	move.w	d5,d4							;  4(1/0)
	; NOTE: preconditions (1), (2), (3), (4) and (5) now valid.
	if 1==1
		; TODO: do we even need this? This would point to an unreliable
		; compressor which should be fixed...
		; Do we have a copy count higher than the number of characters remaining?
		cmp.w	d1,d5						;  4(1/0)
		; Branch if not.
		bls.s	.got_count2					; T: 10(2/0); N:  8(1/0)
		; Cap count to number of bytes remaining.
		move.w	d1,d5						;  4(1/0)

.got_count2:
	endif
	; Strip off low bit, as we will write words.
	andi.b	#$FE,d5							;  8(2/0)
	; Prepare for loop unroll.
	neg.w	d5								;  4(1/0)
	; Copy with lookup table.
	jsr	(a3,d5.w)							; 22(2/2)
	; PRECONDITIONS: (1), (2), (4) and (5) still hold; (3) is replaced by:
	; (3')	the buffer (d6) is empty.
	; NOTE: because of the 5 preconditions above, then we have the following:
	; (B)	if the copy count was even, we haven't finished the decompression.
	; Therefore, if we have reached the end of file, we don't have to worry
	; about the low bit of the copy count we saved above.
	; Factor in all bytes we wrote.
	sub.w	d4,d1							;  4(1/0)
	; Branch if decompression is over.
	beq.s	SNKDecEnd						; T: 10(2/0); N:  8(1/0)
	; To use some common code.
	addq.w	#1,d4							;  4(1/0)
	; Was the copy count an odd number?
	moveq	#1,d5							;  4(1/0)
	and.w	d4,d5							;  4(1/0)
	; Branch if yes.
	bne.s	.no_single_byte					; T: 10(2/0); N:  8(1/0)
	; PRECONDITIONS: all of (2), (3'), (4) and (5) still hold. Instead of (1),
	; we have:
	; (1')	there is an odd number of bytes left to decompress.
	; Implicitly print byte at d3 to buffer at d6 by doing (almost) nothing.
	; Add a byte back to count because we haven't put it into the output stream.
	addq.w	#1,d1							;  4(1/0)
	; PRECONDITIONS: all of (1), (2), (3), (4) and (5) hold.
	; Was the copy count 255?
	addq.b	#1,d4							;  4(1/0)
	; Branch if not.
	bne.s	.main_loop						; T: 10(2/0); N:  8(1/0)
	; We need to read in a new character, and we have one printed to the buffer.
	; Read and print new character to the buffer.
	move.b	(a0)+,d6						;  8(2/0)
	; Write both to output stream.
	move.w	d6,(a2)							;  8(1/1)
	subq.w	#2,d1							;  4(1/0)
	; Branch if decompression was not finished.
	bne.s	.main_loop_buffer_clear			; T: 10(2/0); N:  8(1/0)

SNKDecEnd:
	addq.w	#2,sp							;  8(1/0)
	movem.l	(sp)+,d0/d1/d3-d6/a2-a3			; 76(19/0)
	move.w	#$2000,sr						; 16(2/0)
	rts
; ===========================================================================
SNKDec_CopyLUT:
    rept 127
	move.w	d6,(a2)							;  8(1/1)
    endm
SNKDec_CopyLUT_End:
	rts
; ===========================================================================
