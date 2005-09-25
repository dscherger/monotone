#include <stdio.h>

int main()
{ int c;
  puts("Ready.");
  while ((c=getchar())!=-1) 
  { if (('A'<=c && c<='Z') || ('a'<=c && c<='z'))
      putchar((c&0x1f)<=13?(c+13):(c-13));
    else
      putchar(c);
  }
  return 0;
}

