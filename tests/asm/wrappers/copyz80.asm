	cpu Z80
	org 1000h
	db "MDZ8"
	dw Entry,4000h,0100h
Entry:
	ld bc,4
	ldir
	ret
