# split_s390_e4.s: s390 specific test case for -fsplit-stack -
# esa mode, jl conditional call, ahi.

	.text

	.global	fn1
	.type	fn1,@function
fn1:
	.cfi_startproc
	ear	%r1, %a0
	l	%r1, 0x20(%r1)
	ahi	%r1, 0x1000
	cr	%r15, %r1
	jl	.L7
.L4:
	stm	%r13, %r15, 0x34(%r15)
	.cfi_offset	%r13, -0x2c
	.cfi_offset	%r14, -0x28
	.cfi_offset	%r15, -0x24
	ahi	%r15, -0x60
	.cfi_adjust_cfa_offset	0x60
	basr	%r13, %r0
.L5:
	l	%r1, .L6-.L5(%r13)
	bas	%r14, 0(%r13, %r1)
	lm	%r13, %r15, 0x94(%r15)
	.cfi_restore	%r13
	.cfi_restore	%r14
	.cfi_restore	%r15
	.cfi_adjust_cfa_offset	-0x60
	br	%r14
	.align	4
.L6:
	.long	fn2-.L5
.L7:
	basr	%r1, %r0
.L1:
	a	%r1, .L2-.L1(%r1)
	basr	%r1, %r1
	.align	4
.L3:
	.long	0x1000
	.long	0
	.long	.L4-.L3
.L2:
	.long	__morestack-.L1
	.cfi_endproc
	.size	fn1,. - fn1

	.section	.note.GNU-stack,"",@progbits
	.section	.note.GNU-split-stack,"",@progbits
