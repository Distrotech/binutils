	.text
	.globl	foo
	.type	foo, @function
foo:
	call bar@PLT
	.size	foo, .-foo

	.section	.rodata,"a",@progbits
	.space 0x40000000
	.space 0x3fdfff14
	.section	.note.GNU-stack,"",@progbits
