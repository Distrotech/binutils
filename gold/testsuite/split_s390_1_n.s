# split_s390_n.s: s390 specific test case for -fsplit-stack -
# no stack frame

	.text

	.global	fn1
	.type	fn1,@function
fn1:
	.cfi_startproc
	nopr	%r15
	larl	%r2, fn2
	br	%r14
	.cfi_endproc

	.size	fn1,. - fn1

	.section	.note.GNU-stack,"",@progbits
	.section	.note.GNU-split-stack,"",@progbits
