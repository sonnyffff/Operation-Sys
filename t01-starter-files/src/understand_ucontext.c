// You may need this next line for variables like REG_RIP, etc. found in
// ucontext.h
#define _GNU_SOURCE

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <ucontext.h>
#include <unistd.h>

#ifndef __x86_64__
#error "Do this project on a 64-bit x86-64 linux machine"
#endif

#if __WORDSIZE != 64
#error "word size should be 64 bits"
#endif

/* zero out the context */
ucontext_t my_context = { 0 };

int
get_start_end(long* start, long* end)
{
  char filename_buffer[128];
  sprintf(filename_buffer, "/proc/%d/maps", getpid());
  FILE* f = fopen(filename_buffer, "r");
  if (f == NULL) {
    fprintf(stderr, "Could not open file %s.\n", filename_buffer);
    return -1;
  }

  char line_buffer[256];
  char ex = 0;
  int matches;
  do {
    if (fgets(line_buffer, sizeof(line_buffer), f) != NULL) {
      matches = sscanf(line_buffer, "%lx-%lx %*c%*c%c%*c ", start, end, &ex);
    } else {
      matches = EOF;
    }
  } while (ex != 'x' && matches > 0);

  if (ex != 'x') {
    fprintf(stderr, "Did not find an executable region in the process maps.\n");
    *start = *end = 0;

    return -1;
  }

  fclose(f);
  return 0;
}

void
call_setcontext(ucontext_t* context)
{
  int err = setcontext(context);
  assert(!err);
}

int
main(int argc, char** argv)
{
  // We declare setcontext_called to be volatile so that the compiler will
  // make sure to store it on the stack and not in a register. This is
  // ESSENTIAL, or else the code in this function may run in an infinite
  // loop.
  volatile int setcontext_called = 0;

  // Get context: make sure to read the man page of getcontext in detail,
  // or else you will not be able to answer the questions below. */
  int err = getcontext(&my_context);
  assert(!err);

  printf("%s: setcontext_called = %d\n", __FUNCTION__, setcontext_called);
  if (setcontext_called == 1) {
    exit(0);
  }

  long start, end;
  get_start_end(&start, &end);
  fprintf(stdout, "start = 0x%lx\n", start);
  fprintf(stdout, "end = 0x%lx\n", end);

  // You may find it helpful to replace the -1's below with the correct code in
  // order to answer questions from the handout
  printf("ucontext_t size = %ld bytes\n", (long int)-1);
  printf("memory address of main() = 0x%lx\n", (unsigned long)-1);
  printf("memory address of the program counter (RIP) saved "
         "in mycontext = 0x%lx\n",
         (unsigned long)-1);
  printf("argc = %d\n", -1);
  printf("argv = %p\n", (void*)-1);
  printf("memory address of the variable setcontext_called = %p\n", (void*)-1);
  printf("memory address of the variable err = %p\n", (void*)-1);
  printf("number of bytes pushed to the stack between setcontext_called "
         "and err = %ld\n",
         (unsigned long)-1);

  printf("stack pointer register (RSP) stored in mycontext = 0x%lx\n",
         (unsigned long)-1);

  printf("number of bytes between err and the saved stack in mycontext "
         "= %ld\n",
         (unsigned long)-1);
  printf("value of uc_stack.ss_sp = 0x%lx\n", (unsigned long)-1);

  setcontext_called = 1;
  call_setcontext(&my_context);
  assert(0);

  return 0;
}
