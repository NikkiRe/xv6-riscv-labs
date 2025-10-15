#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

void leaky_function() {
  sbrk(4096); 
  sbrk(4096);
}

int
main(int argc, char *argv[])
{
  unsigned long before = 0;
  unsigned long after = 0;
  int pid;

  if(argc < 2) {
    pid = getpid();
  } else {
    pid = atoi(argv[1]);
  }

  allocstat(pid, (uint64)&before);
  printf("[memtrack] pid=%d, before=%lu pages\n", pid, before);

  leaky_function();

  allocstat(pid, (uint64)&after);
  printf("[memtrack] pid=%d, after =%lu pages\n", pid, after);

  long delta = (long)after - (long)before;

  if(delta > 0)
    printf("[memtrack] Leaked %ld pages (~%ld bytes)\n", delta, delta * 4096L);
  else if(delta < 0)
    printf("[memtrack] Freed %ld pages (~%ld bytes)\n", -delta, -delta * 4096L);
  else
    printf("[memtrack] No net memory change.\n");

  exit(0);
}
