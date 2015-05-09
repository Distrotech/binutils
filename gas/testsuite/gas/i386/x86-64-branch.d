#as: -J
#objdump: -dw
#name: x86-64 branch

.*: +file format .*

Disassembly of section .text:

0+ <.text>:
[ 	]*[a-f0-9]+:	ff d0                	callq  \*%rax
[ 	]*[a-f0-9]+:	ff d0                	callq  \*%rax
[ 	]*[a-f0-9]+:	66 ff d0             	callw  \*%ax
[ 	]*[a-f0-9]+:	66 ff d0             	callw  \*%ax
[ 	]*[a-f0-9]+:	66 ff 10             	callw  \*\(%rax\)
[ 	]*[a-f0-9]+:	ff e0                	jmpq   \*%rax
[ 	]*[a-f0-9]+:	ff e0                	jmpq   \*%rax
[ 	]*[a-f0-9]+:	66 ff e0             	jmpw   \*%ax
[ 	]*[a-f0-9]+:	66 ff e0             	jmpw   \*%ax
[ 	]*[a-f0-9]+:	66 ff 20             	jmpw   \*\(%rax\)
[ 	]*[a-f0-9]+:	e8 (00|5b) 00 (00|10) 00       	callq  (0x1f|10007a <.text\+0x10007a>)
[ 	]*[a-f0-9]+:	e9 (00|60) 00 (00|10) 00       	jmpq   (0x24|100084 <.text\+0x100084>)
[ 	]*[a-f0-9]+:	66 e8 00 00 00 00    	data16 callq (0x2a|2a <.text\+0x2a>)
[ 	]*[a-f0-9]+:	66 e9 00 00 00 00    	data16 jmpq (0x30|30 <.text\+0x30>)
[ 	]*[a-f0-9]+:	ff d0                	callq  \*%rax
[ 	]*[a-f0-9]+:	ff d0                	callq  \*%rax
[ 	]*[a-f0-9]+:	66 ff d0             	callw  \*%ax
[ 	]*[a-f0-9]+:	66 ff d0             	callw  \*%ax
[ 	]*[a-f0-9]+:	66 ff 10             	callw  \*\(%rax\)
[ 	]*[a-f0-9]+:	ff e0                	jmpq   \*%rax
[ 	]*[a-f0-9]+:	ff e0                	jmpq   \*%rax
[ 	]*[a-f0-9]+:	66 ff e0             	jmpw   \*%ax
[ 	]*[a-f0-9]+:	66 ff e0             	jmpw   \*%ax
[ 	]*[a-f0-9]+:	66 ff 20             	jmpw   \*\(%rax\)
[ 	]*[a-f0-9]+:	e8 (00|8b) 00 (00|10) 00       	callq  (0x4f|1000da <.text\+0x1000da>)
[ 	]*[a-f0-9]+:	e9 (00|90) 00 (00|10) 00       	jmpq   (0x54|1000e4 <.text\+0x1000e4>)
#pass
