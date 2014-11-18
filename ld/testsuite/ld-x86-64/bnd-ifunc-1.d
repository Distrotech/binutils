#as: --64 -madd-bnd-prefix
#ld: -shared -melf_x86_64 -z bndplt
#objdump: -dw

#...
[ 	]*[a-f0-9]+:	f2 e8 28 00 00 00    	bnd callq 230 <\*ABS\*\+0x200@plt>
#pass
