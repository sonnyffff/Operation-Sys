#include <stdio.h>
#include <stdlib.h>

void
Spin(int secs)
{
  int i, iters = secs * 95000000; // Guess
  for (i = 0; i < iters; i++)
    ;
}

int
main(int argc, char* argv[])
{
  if (argc != 2) {
    fprintf(stderr, "usage: %s <string>\n", argv[0]);
    exit(1);
  }

  char* str = argv[1];
  while (1) {
    Spin(1);
    printf("%s\n", str);
  }

  return 0;
}
