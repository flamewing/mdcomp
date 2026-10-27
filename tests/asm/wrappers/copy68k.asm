	cpu 68000
	org $1000
	dc.b "MD68"
	dc.l Entry,$20000,$100
Entry:
	moveq #3,d0
.loop:
	move.b (a0)+,(a1)+
	dbra d0,.loop
	rts
