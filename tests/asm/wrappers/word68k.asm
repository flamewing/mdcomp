	cpu 68000
	org $1000
	dc.b "MD68"
	dc.l Entry,$20001,$100
Entry:
	move.w (a0)+,(a1)+
	rts
