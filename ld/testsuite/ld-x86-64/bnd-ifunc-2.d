#as: --64 -madd-bnd-prefix
#ld: -shared -melf_x86_64 -z bndplt
#objdump: -dw

.*: +file format .*


Disassembly of section .text:

0+2c8 <resolve1>:
[ 	]*[a-f0-9]+:	f2 e8 6a 00 00 00    	bnd callq 338 <func1@plt>

0+2ce <g1>:
[ 	]*[a-f0-9]+:	f2 e9 74 00 00 00    	bnd jmpq 348 <\*ABS\*\+0x2c8@plt>

0+2d4 <resolve2>:
[ 	]*[a-f0-9]+:	f2 e8 66 00 00 00    	bnd callq 340 <func2@plt>

0+2da <g2>:
[ 	]*[a-f0-9]+:	f2 e9 50 00 00 00    	bnd jmpq 330 <\*ABS\*\+0x2d4@plt>

Disassembly of section .plt:

0+2e0 <.plt>:
[ 	]*[a-f0-9]+:	ff 35 62 01 20 00    	pushq  0x200162\(%rip\)        # 200448 <_GLOBAL_OFFSET_TABLE_\+0x8>
[ 	]*[a-f0-9]+:	f2 ff 25 63 01 20 00 	bnd jmpq \*0x200163\(%rip\)        # 200450 <_GLOBAL_OFFSET_TABLE_\+0x10>
[ 	]*[a-f0-9]+:	0f 1f 00             	nopl   \(%rax\)
[ 	]*[a-f0-9]+:	68 03 00 00 00       	pushq  \$0x3
[ 	]*[a-f0-9]+:	f2 e9 e5 ff ff ff    	bnd jmpq 2e0 <g2\+0x6>
[ 	]*[a-f0-9]+:	0f 1f 44 00 00       	nopl   0x0\(%rax,%rax,1\)
[ 	]*[a-f0-9]+:	68 00 00 00 00       	pushq  \$0x0
[ 	]*[a-f0-9]+:	f2 e9 d5 ff ff ff    	bnd jmpq 2e0 <g2\+0x6>
[ 	]*[a-f0-9]+:	0f 1f 44 00 00       	nopl   0x0\(%rax,%rax,1\)
[ 	]*[a-f0-9]+:	68 01 00 00 00       	pushq  \$0x1
[ 	]*[a-f0-9]+:	f2 e9 c5 ff ff ff    	bnd jmpq 2e0 <g2\+0x6>
[ 	]*[a-f0-9]+:	0f 1f 44 00 00       	nopl   0x0\(%rax,%rax,1\)
[ 	]*[a-f0-9]+:	68 02 00 00 00       	pushq  \$0x2
[ 	]*[a-f0-9]+:	f2 e9 b5 ff ff ff    	bnd jmpq 2e0 <g2\+0x6>
[ 	]*[a-f0-9]+:	0f 1f 44 00 00       	nopl   0x0\(%rax,%rax,1\)

Disassembly of section .plt.bnd:

0+330 <\*ABS\*\+0x2d4@plt>:
[ 	]*[a-f0-9]+:	f2 ff 25 21 01 20 00 	bnd jmpq \*0x200121\(%rip\)        # 200458 <_GLOBAL_OFFSET_TABLE_\+0x18>
[ 	]*[a-f0-9]+:	90                   	nop

0+338 <func1@plt>:
[ 	]*[a-f0-9]+:	f2 ff 25 21 01 20 00 	bnd jmpq \*0x200121\(%rip\)        # 200460 <_GLOBAL_OFFSET_TABLE_\+0x20>
[ 	]*[a-f0-9]+:	90                   	nop

0+340 <func2@plt>:
[ 	]*[a-f0-9]+:	f2 ff 25 21 01 20 00 	bnd jmpq \*0x200121\(%rip\)        # 200468 <_GLOBAL_OFFSET_TABLE_\+0x28>
[ 	]*[a-f0-9]+:	90                   	nop

0+348 <\*ABS\*\+0x2c8@plt>:
[ 	]*[a-f0-9]+:	f2 ff 25 21 01 20 00 	bnd jmpq \*0x200121\(%rip\)        # 200470 <_GLOBAL_OFFSET_TABLE_\+0x30>
[ 	]*[a-f0-9]+:	90                   	nop
#pass
