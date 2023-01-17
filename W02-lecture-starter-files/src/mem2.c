#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

void
Spin(int secs)
{
  int i, iters = secs * 450000000; // Guess
  for (i = 0; i < iters; i++)
    ;
}

int
main(int argc, char* argv[])
{
  int* p;

  if (argc != 2) {
    fprintf(stderr, "usage: %s <initial val>\n", argv[0]);
    exit(1);
  }

  p = malloc(sizeof(int));

  *p = atoi(argv[1]);

  while (1) {
    Spin(1);
    *p = *p + 1;
    printf("(%d) p at %p: %d\n", getpid(), p, *p);
  }

  return 0;
}
