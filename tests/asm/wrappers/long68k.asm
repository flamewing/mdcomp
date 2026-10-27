	cpu 68000
	org $1000
	dc.b "MD68"
	dc.l Entry,$20000,$100
Entry:
	move.l (a0)+,(a1)+
	rts
