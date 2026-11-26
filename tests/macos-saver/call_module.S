; call_module.S - calls an After Dark module with the Pascal calling
; convention, for adhost.c:
;   short CallModule(Ptr code, Handle *storage, RgnHandle blankRgn,
;                    short message, void *params);
; C arguments (each in 4 bytes): 4(sp) code, 8 storage, 12 blankRgn,
; 16 message (the word at 18), 20 params.

	.text
	.globl	CallModule
CallModule:
	movem.l	%d2-%d7/%a2-%a6, -(%sp)      ; 44 bytes
	clr.w	-(%sp)                       ; space of the result
	move.l	(54, %sp), -(%sp)            ; storage
	move.l	(62, %sp), -(%sp)            ; blankRgn
	move.w	(72, %sp), -(%sp)            ; message
	move.l	(76, %sp), -(%sp)            ; params
	move.l	(64, %sp), %a0               ; code
	jsr	(%a0)
	move.w	(%sp)+, %d0                  ; the result
	ext.l	%d0
	movem.l	(%sp)+, %d2-%d7/%a2-%a6
	rts
