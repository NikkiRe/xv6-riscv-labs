#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

void
spin(int iterations)
{
  volatile int dummy = 0;
  for (int i = 0; i < iterations; i++) {
    dummy += i;
  }
}

void
cpu_bound_task(void)
{
  printf("[CPU-bound] PID=%d started\n", getpid());
  
  for (int i = 0; i < 20; i++) {
    int prio = getpriority();
    printf("[CPU-bound] PID=%d iteration=%d priority=%d\n", getpid(), i, prio);
    spin(100000);
  }
  
  printf("[CPU-bound] PID=%d finished with priority=%d\n", getpid(), getpriority());
}

void
io_bound_task(void)
{
  printf("[I/O-bound] PID=%d started\n", getpid());
  
  for (int i = 0; i < 20; i++) {
    int prio = getpriority();
    printf("[I/O-bound] PID=%d iteration=%d priority=%d\n", getpid(), i, prio);
    sleep(1);
  }
  
  printf("[I/O-bound] PID=%d finished with priority=%d\n", getpid(), getpriority());
}

void
mixed_task(void)
{
  printf("[Mixed] PID=%d started\n", getpid());
  
  for (int i = 0; i < 15; i++) {
    int prio = getpriority();
    printf("[Mixed] PID=%d iteration=%d priority=%d\n", getpid(), i, prio);
    
    if (i % 3 == 0) {
      sleep(1);
    } else {
      spin(50000);
    }
  }
  
  printf("[Mixed] PID=%d finished with priority=%d\n", getpid(), getpriority());
}

int
main(int argc, char *argv[])
{
  printf("MLFQ Test Started\n");
  printf("=================\n\n");
  
  int pid_cpu = fork();
  if (pid_cpu == 0) {
    cpu_bound_task();
    exit(0);
  }
  
  int pid_io = fork();
  if (pid_io == 0) {
    io_bound_task();
    exit(0);
  }
  
  int pid_mixed = fork();
  if (pid_mixed == 0) {
    mixed_task();
    exit(0);
  }
  
  wait(0);
  wait(0);
  wait(0);
  
  printf("\n=================\n");
  printf("MLFQ Test Complete\n");
  printf("Expected behavior:\n");
  printf("  CPU-bound: priority should increase (0->1->2->3)\n");
  printf("  I/O-bound: priority should stay low (0 or 1)\n");
  printf("  Mixed: priority should vary between levels\n");
  
  exit(0);
}

