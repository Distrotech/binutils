#source: bnd-branch-1.s
#as: --64
#ld: -shared -melf_x86_64 -z bndplt
#objdump: -dw

.*: +file format .*


Disassembly of section .text:

0+2a8 <_start>:
[ 	]*[a-f0-9]+:	f2 e9 82 00 00 00    	bnd jmpq 330 <foo1@plt>
[ 	]*[a-f0-9]+:	e8 6d 00 00 00       	callq  320 <foo2@plt>
[ 	]*[a-f0-9]+:	e9 70 00 00 00       	jmpq   328 <foo3@plt>
[ 	]*[a-f0-9]+:	e8 7b 00 00 00       	callq  338 <foo4@plt>
[ 	]*[a-f0-9]+:	f2 e8 65 00 00 00    	bnd callq 328 <foo3@plt>
[ 	]*[a-f0-9]+:	e9 70 00 00 00       	jmpq   338 <foo4@plt>

Disassembly of section .plt:

0+2d0 <.plt>:
[ 	]*[a-f0-9]+:	ff 35 62 01 20 00    	pushq  0x200162\(%rip\)        # 200438 <_GLOBAL_OFFSET_TABLE_\+0x8>
[ 	]*[a-f0-9]+:	f2 ff 25 63 01 20 00 	bnd jmpq \*0x200163\(%rip\)        # 200440 <_GLOBAL_OFFSET_TABLE_\+0x10>
[ 	]*[a-f0-9]+:	0f 1f 00             	nopl   \(%rax\)
[ 	]*[a-f0-9]+:	68 00 00 00 00       	pushq  \$0x0
[ 	]*[a-f0-9]+:	f2 e9 e5 ff ff ff    	bnd jmpq 2d0 <_start\+0x28>
[ 	]*[a-f0-9]+:	0f 1f 44 00 00       	nopl   0x0\(%rax,%rax,1\)
[ 	]*[a-f0-9]+:	68 01 00 00 00       	pushq  \$0x1
[ 	]*[a-f0-9]+:	f2 e9 d5 ff ff ff    	bnd jmpq 2d0 <_start\+0x28>
[ 	]*[a-f0-9]+:	0f 1f 44 00 00       	nopl   0x0\(%rax,%rax,1\)
[ 	]*[a-f0-9]+:	68 02 00 00 00       	pushq  \$0x2
[ 	]*[a-f0-9]+:	f2 e9 c5 ff ff ff    	bnd jmpq 2d0 <_start\+0x28>
[ 	]*[a-f0-9]+:	0f 1f 44 00 00       	nopl   0x0\(%rax,%rax,1\)
[ 	]*[a-f0-9]+:	68 03 00 00 00       	pushq  \$0x3
[ 	]*[a-f0-9]+:	f2 e9 b5 ff ff ff    	bnd jmpq 2d0 <_start\+0x28>
[ 	]*[a-f0-9]+:	0f 1f 44 00 00       	nopl   0x0\(%rax,%rax,1\)

Disassembly of section .plt.bnd:

0+320 <foo2@plt>:
[ 	]*[a-f0-9]+:	f2 ff 25 21 01 20 00 	bnd jmpq \*0x200121\(%rip\)        # 200448 <_GLOBAL_OFFSET_TABLE_\+0x18>
[ 	]*[a-f0-9]+:	90                   	nop

0+328 <foo3@plt>:
[ 	]*[a-f0-9]+:	f2 ff 25 21 01 20 00 	bnd jmpq \*0x200121\(%rip\)        # 200450 <_GLOBAL_OFFSET_TABLE_\+0x20>
[ 	]*[a-f0-9]+:	90                   	nop

0+330 <foo1@plt>:
[ 	]*[a-f0-9]+:	f2 ff 25 21 01 20 00 	bnd jmpq \*0x200121\(%rip\)        # 200458 <_GLOBAL_OFFSET_TABLE_\+0x28>
[ 	]*[a-f0-9]+:	90                   	nop

0+338 <foo4@plt>:
[ 	]*[a-f0-9]+:	f2 ff 25 21 01 20 00 	bnd jmpq \*0x200121\(%rip\)        # 200460 <_GLOBAL_OFFSET_TABLE_\+0x30>
[ 	]*[a-f0-9]+:	90                   	nop
#pass
