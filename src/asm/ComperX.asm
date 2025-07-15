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
	set comperx_return_reg,0			; rts on end
ComperXDec:
	include "ComperX_internal.asm"
	; ComperX never returns by falling off the end of ComperX_internal.asm.
	; Therefore, no need for an 'rts' instruction here.
; =============================================================================
