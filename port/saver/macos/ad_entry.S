; ad_entry.S - the entry point of an After Dark module (68k code
; resource 'ADgm').
;
; After Dark calls the module with the Pascal calling convention:
;   pascal OSErr main(Handle *storage, RgnHandle blankRgn, short message,
;                     GMParamBlockPtr params);
; The caller pushes the space of the result, then the arguments from left
; to right. The callee removes the arguments. mac-sdk's start-coderes.S
; relocates the module and jumps here with the stack of the caller. This
; code calls ADModuleMain (C calling convention), puts the result into the
; space of the result and removes the arguments.
;
; The stack at entry:
;    0(sp) return address    4(sp) params     8(sp) message (word)
;   10(sp) blankRgn         14(sp) storage   18(sp) result (word)

	.text
	.globl	main
main:
	move.l	(4, %sp), -(%sp)             ; params
	move.w	(12, %sp), %d0               ; message
	ext.l	%d0
	move.l	%d0, -(%sp)
	move.l	(18, %sp), -(%sp)            ; blankRgn
	move.l	(26, %sp), -(%sp)            ; storage
	jsr	ADModuleMain
	lea	(16, %sp), %sp
	move.w	%d0, (18, %sp)               ; the result
	move.l	(%sp)+, %a0                  ; the return address
	lea	(14, %sp), %sp               ; remove the arguments
	jmp	(%a0)
