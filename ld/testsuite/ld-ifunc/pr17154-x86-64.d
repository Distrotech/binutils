#source: pr17154-x86.s
#as: --64
#ld: -shared -melf_x86_64
#objdump: -dw
#target: x86_64-*-*

.*: +file format .*


Disassembly of section .text:

0+2c8 <resolve1>:
[ 	]*[a-f0-9]+:	e8 33 00 00 00       	callq  300 <func1@plt>

0+2cd <g1>:
[ 	]*[a-f0-9]+:	e9 4e 00 00 00       	jmpq   320 <\*ABS\*\+0x2c8@plt>

0+2d2 <resolve2>:
[ 	]*[a-f0-9]+:	e8 39 00 00 00       	callq  310 <func2@plt>

0+2d7 <g2>:
[ 	]*[a-f0-9]+:	e9 14 00 00 00       	jmpq   2f0 <\*ABS\*\+0x2d2@plt>

Disassembly of section .plt:

0+2e0 <\*ABS\*\+0x2d2@plt-0x10>:
[ 	]*[a-f0-9]+:	ff 35 42 01 20 00    	pushq  0x200142\(%rip\)        # 200428 <_GLOBAL_OFFSET_TABLE_\+0x8>
[ 	]*[a-f0-9]+:	ff 25 44 01 20 00    	jmpq   \*0x200144\(%rip\)        # 200430 <_GLOBAL_OFFSET_TABLE_\+0x10>
[ 	]*[a-f0-9]+:	0f 1f 40 00          	nopl   0x0\(%rax\)

0+2f0 <\*ABS\*\+0x2d2@plt>:
[ 	]*[a-f0-9]+:	ff 25 42 01 20 00    	jmpq   \*0x200142\(%rip\)        # 200438 <_GLOBAL_OFFSET_TABLE_\+0x18>
[ 	]*[a-f0-9]+:	68 03 00 00 00       	pushq  \$0x3
[ 	]*[a-f0-9]+:	e9 e0 ff ff ff       	jmpq   2e0 <g2\+0x9>

0+300 <func1@plt>:
[ 	]*[a-f0-9]+:	ff 25 3a 01 20 00    	jmpq   \*0x20013a\(%rip\)        # 200440 <_GLOBAL_OFFSET_TABLE_\+0x20>
[ 	]*[a-f0-9]+:	68 00 00 00 00       	pushq  \$0x0
[ 	]*[a-f0-9]+:	e9 d0 ff ff ff       	jmpq   2e0 <g2\+0x9>

0+310 <func2@plt>:
[ 	]*[a-f0-9]+:	ff 25 32 01 20 00    	jmpq   \*0x200132\(%rip\)        # 200448 <_GLOBAL_OFFSET_TABLE_\+0x28>
[ 	]*[a-f0-9]+:	68 01 00 00 00       	pushq  \$0x1
[ 	]*[a-f0-9]+:	e9 c0 ff ff ff       	jmpq   2e0 <g2\+0x9>

0+320 <\*ABS\*\+0x2c8@plt>:
[ 	]*[a-f0-9]+:	ff 25 2a 01 20 00    	jmpq   \*0x20012a\(%rip\)        # 200450 <_GLOBAL_OFFSET_TABLE_\+0x30>
[ 	]*[a-f0-9]+:	68 02 00 00 00       	pushq  \$0x2
[ 	]*[a-f0-9]+:	e9 b0 ff ff ff       	jmpq   2e0 <g2\+0x9>
#pass
