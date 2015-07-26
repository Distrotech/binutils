extern void bar (void);
extern void foo (void);

__attribute__ ((noinline, noclone))
int
foo_p (void)
{
  return (long) &foo == 0x12345678 ? 1 : 0;
}

int
main (void)
{
  foo ();
  foo_p ();
  bar ();
  return 0;
}
