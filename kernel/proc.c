#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"

struct cpu cpus[NCPU];

struct proc *initproc;

// Doubly-linked circular list of processes.
struct proc proc_table;

// Active process count (enforces NPROC limit).
static struct {
  struct spinlock lock;
  int count;
} proccnt;

static void
proc_table_init(struct proc* p)
{
  p->pid = -1;
  p->state = UNUSED;
  p->next = p;
  p->last = p;
}

static void
proc_table_push(struct proc* head, struct proc* p)
{
  p->next = head->next;
  p->last = head;
  head->next->last = p;
  head->next = p;
}

static void
proc_table_remove(struct proc* p)
{
  p->last->next = p->next;
  p->next->last = p->last;
  p->next = 0;
  p->last = 0;
}

int nextpid = 1;
struct spinlock pid_lock;

extern void forkret(void);
static void freeproc(struct proc *p);
extern char trampoline[]; // trampoline.S

// Process list lock.
struct spinlock proc_table_lock;

void
proc_mapstacks(pagetable_t kpgtbl)
{
  (void)kpgtbl;
}

void
procinit(void)
{
  initlock(&pid_lock, "nextpid");
  initlock(&proc_table_lock, "list_lock");
  initlock(&proccnt.lock, "proccnt");
  proccnt.count = 0;
  proc_table_init(&proc_table);
}

// Requires interrupts disabled.
int
cpuid()
{
  int id = r_tp();
  return id;
}

// Return current CPU (interrupts must be disabled).
struct cpu*
mycpu(void)
{
  int id = cpuid();
  struct cpu *c = &cpus[id];
  return c;
}

// Return current proc* or 0.
struct proc*
myproc(void)
{
  push_off();
  struct cpu *c = mycpu();
  struct proc *p = c->proc;
  pop_off();
  return p;
}

int
allocpid(void)
{
  int pid;
  acquire(&pid_lock);
  pid = nextpid;
  nextpid = nextpid + 1;
  release(&pid_lock);
  return pid;
}

// Allocate and initialize new process.
static struct proc*
allocproc(void)
{
  acquire(&proccnt.lock);
  if (proccnt.count >= NPROC) {
    release(&proccnt.lock);
    return 0;
  }
  proccnt.count++;
  release(&proccnt.lock);

  struct proc *p = bd_malloc(sizeof(struct proc));
  if (!p) {
    acquire(&proccnt.lock);
    proccnt.count--;
    release(&proccnt.lock);
    return 0;
  }

  memset(p, 0, sizeof(struct proc));
  initlock(&p->lock, "proc");
  p->pid = allocpid();
  p->state = USED;

  if ((p->kstack = (uint64)kalloc()) == 0) {
    freeproc(p);
    return 0;
  }

  if((p->trapframe = (struct trapframe *)kalloc()) == 0){
    freeproc(p);
    return 0;
  }

  p->pagetable = proc_pagetable(p);
  if(p->pagetable == 0){
    freeproc(p);
    return 0;
  }

  // Context returns to forkret, SP at top of kstack.
  memset(&p->context, 0, sizeof(p->context));
  p->context.ra = (uint64)forkret;
  p->context.sp = p->kstack + PGSIZE;

  return p;
}

// Free process resources and descriptor.
// May be called with or without proc_table_lock held.
static void
freeproc(struct proc *p)
{
  // Remove from list if present, respecting lock state.
  if (p->next && p->last) {
    int have = holding(&proc_table_lock);
    if (!have)
      acquire(&proc_table_lock);
    if (p->next && p->last)  // Re-check under lock.
      proc_table_remove(p);
    if (!have)
      release(&proc_table_lock);
  }

  if (p->kstack) {
    kfree((void *)p->kstack);
    p->kstack = 0;
  }

  if (p->trapframe){
    kfree((void*)p->trapframe);
    p->trapframe = 0;
  }

  if (p->pagetable){
    proc_freepagetable(p->pagetable, p->sz);
    p->pagetable = 0;
  }

  p->sz = 0;
  p->pid = 0;
  p->parent = 0;
  p->name[0] = 0;
  p->chan = 0;
  p->killed = 0;
  p->xstate = 0;
  p->state = UNUSED;

  bd_free((void *)p);

  acquire(&proccnt.lock);
  if (proccnt.count > 0)
    proccnt.count--;
  release(&proccnt.lock);
}

// Create user pagetable with TRAMPOLINE/TRAPFRAME.
pagetable_t
proc_pagetable(struct proc *p)
{
  pagetable_t pagetable = uvmcreate();
  if(pagetable == 0)
    return 0;

  if(mappages(pagetable, TRAMPOLINE, PGSIZE,
              (uint64)trampoline, PTE_R | PTE_X) < 0){
    uvmfree(pagetable, 0);
    return 0;
  }

  if(mappages(pagetable, TRAPFRAME, PGSIZE,
              (uint64)(p->trapframe), PTE_R | PTE_W) < 0){
    uvmunmap(pagetable, TRAMPOLINE, 1, 0);
    uvmfree(pagetable, 0);
    return 0;
  }

  return pagetable;
}

// Free user pagetable and memory.
void
proc_freepagetable(pagetable_t pagetable, uint64 sz)
{
  uvmunmap(pagetable, TRAMPOLINE, 1, 0);
  uvmunmap(pagetable, TRAPFRAME, 1, 0);
  uvmfree(pagetable, sz);
}

// initcode.S
uchar initcode[] = {
  0x17, 0x05, 0x00, 0x00, 0x13, 0x05, 0x45, 0x02,
  0x97, 0x05, 0x00, 0x00, 0x93, 0x85, 0x35, 0x02,
  0x93, 0x08, 0x70, 0x00, 0x73, 0x00, 0x00, 0x00,
  0x93, 0x08, 0x20, 0x00, 0x73, 0x00, 0x00, 0x00,
  0xef, 0xf0, 0x9f, 0xff, 0x2f, 0x69, 0x6e, 0x69,
  0x74, 0x00, 0x00, 0x24, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00
};

// Initialize first user process.
void
userinit(void)
{
  struct proc *p;

  p = allocproc();
  if (p == 0)
    panic("userinit: allocproc failed");

  initproc = p;

  uvmfirst(p->pagetable, initcode, sizeof(initcode));
  p->sz = PGSIZE;

  p->trapframe->epc = 0;
  p->trapframe->sp = PGSIZE;

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");

  acquire(&proc_table_lock);
  proc_table_push(&proc_table, p);
  p->state = RUNNABLE;
  release(&proc_table_lock);
}

// Grow or shrink process memory.
int
growproc(int n)
{
  uint64 sz;
  struct proc *p = myproc();

  sz = p->sz;
  if(n > 0){
    if((sz = uvmalloc(p->pagetable, sz, sz + n, PTE_W)) == 0) {
      return -1;
    }
  } else if(n < 0){
    sz = uvmdealloc(p->pagetable, sz, sz + n);
  }
  p->sz = sz;
  return 0;
}

// Create child process.
int
fork(void)
{
  int i, pid;
  struct proc *np;
  struct proc *p = myproc();

  if((np = allocproc()) == 0){
    return -1;
  }

  if(uvmcopy(p->pagetable, np->pagetable, p->sz) < 0){
    freeproc(np);
    return -1;
  }
  np->sz = p->sz;

  *(np->trapframe) = *(p->trapframe);
  np->trapframe->a0 = 0;  // Child returns 0.

  for(i = 0; i < NOFILE; i++)
    if(p->ofile[i])
      np->ofile[i] = filedup(p->ofile[i]);
  np->cwd = idup(p->cwd);

  safestrcpy(np->name, p->name, sizeof(p->name));

  pid = np->pid;

  np->parent = p;
  acquire(&proc_table_lock);
  proc_table_push(&proc_table, np);
  np->state = RUNNABLE;
  release(&proc_table_lock);

  return pid;
}

// Wakeup processes on chan (must hold proc_table_lock).
void
wakeup_holding_proc_table_lock(void* chan)
{
  struct proc *self = myproc();
  for (struct proc* p = proc_table.next; p != &proc_table; p = p->next) {
    if (p != self && p->state == SLEEPING && p->chan == chan) {
      p->state = RUNNABLE;
    }
  }
}

// Give orphaned children to init (must hold proc_table_lock).
void
reparent(struct proc *p)
{
  for (struct proc *pp = proc_table.next; pp != &proc_table; pp = pp->next) {
    if (pp->parent == p) {
      pp->parent = initproc;
      wakeup_holding_proc_table_lock(initproc);
    }
  }
}

// Exit current process.
void
exit(int status)
{
  struct proc *p = myproc();

  if (p == initproc)
    panic("init exiting");

  for (int fd = 0; fd < NOFILE; fd++) {
    if (p->ofile[fd]) {
      struct file *f = p->ofile[fd];
      fileclose(f);
      p->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(p->cwd);
  end_op();
  p->cwd = 0;

  acquire(&proc_table_lock);
  reparent(p);
  wakeup_holding_proc_table_lock(p->parent);
  p->xstate = status;
  p->state = ZOMBIE;
  sched();
  panic("zombie exit");
}

// Wait for child to exit.
int
wait(uint64 addr)
{
  struct proc *pp;
  int havekids, pid;
  struct proc *p = myproc();

  acquire(&proc_table_lock);
  for(;;){
    havekids = 0;
    for (struct proc *it = proc_table.next; it != &proc_table; it = it->next) {
      pp = it;
      if (pp->parent == p) {
        havekids = 1;
        if (pp->state == ZOMBIE) {
          pid = pp->pid;
          if (addr != 0 && copyout(p->pagetable, addr, (char *)&pp->xstate,
                                   sizeof(pp->xstate)) < 0) {
            release(&proc_table_lock);
            return -1;
          }
          freeproc(pp);
          release(&proc_table_lock);
          return pid;
        }
      }
    }

    if (!havekids || __atomic_load_n(&p->killed, __ATOMIC_ACQUIRE)) {
      release(&proc_table_lock);
      return -1;
    }

    sleep(p, &proc_table_lock);
  }
}

// Scheduler: pick RUNNABLE process and switch to it.
void
scheduler(void)
{
  struct proc *p;
  struct cpu *c = mycpu();

  c->proc = 0;
  for(;;){
    intr_on();

    acquire(&proc_table_lock);
    p = 0;
    int found = 0;
    for (struct proc *it = proc_table.next; it != &proc_table; it = it->next) {
      if (it->state == RUNNABLE) {
        p = it;
        found = 1;
        break;
      }
    }
    
    if (found) {
      p->state = RUNNING;
      c->proc = p;
      swtch(&c->context, &p->context);
      c->proc = 0;
    }
    release(&proc_table_lock);

    if (!found) {
      intr_on();
      asm volatile("wfi");
    }
  }
}

// Switch to scheduler (must hold proc_table_lock, state != RUNNING).
void
sched(void)
{
  int intena;
  struct proc *p = myproc();

  if(!holding(&proc_table_lock))
    panic("sched p->lock");
  if(mycpu()->noff != 1)
    panic("sched locks");
  if(p->state == RUNNING)
    panic("sched running");
  if(intr_get())
    panic("sched interruptible");

  intena = mycpu()->intena;
  swtch(&p->context, &mycpu()->context);
  mycpu()->intena = intena;
}

// Yield CPU.
void
yield(void)
{
  struct proc *p = myproc();
  acquire(&proc_table_lock);
  p->state = RUNNABLE;
  sched();
  release(&proc_table_lock);
}

// First run of child after fork().
void
forkret(void)
{
  static int first = 1;

  release(&proc_table_lock);

  if (first) {
    first = 0;
    __sync_synchronize();
    fsinit(ROOTDEV);
  }

  usertrapret();
}

// Release lock and sleep on chan, reacquire lock when awakened.
void
sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();

  if(lk != &proc_table_lock) {
    acquire(&proc_table_lock);
    release(lk);
  }

  p->chan = chan;
  p->state = SLEEPING;
  sched();
  p->chan = 0;

  if(lk != &proc_table_lock) {
    release(&proc_table_lock);
    acquire(lk);
  }
}

// Wake up processes sleeping on chan.
void
wakeup(void *chan)
{
  acquire(&proc_table_lock);
  wakeup_holding_proc_table_lock(chan);
  release(&proc_table_lock);
}

// Kill process.
int
kill(int pid)
{
  acquire(&proc_table_lock);
  for(struct proc *p = proc_table.next; p != &proc_table; p = p->next) {
    if(p->pid == pid){
      __atomic_store_n(&p->killed, 1, __ATOMIC_RELEASE);
      if(p->state == SLEEPING){
        p->state = RUNNABLE;
      }
      release(&proc_table_lock);
      return 0;
    }
  }
  release(&proc_table_lock);
  return -1;
}

void
setkilled(struct proc *p)
{
  __atomic_store_n(&p->killed, 1, __ATOMIC_RELEASE);
}

int
killed(struct proc *p)
{
  return __atomic_load_n(&p->killed, __ATOMIC_ACQUIRE);
}

// Fast check if user address is valid (avoids page table walks).
static inline int
uaddr_in_range(struct proc *p, uint64 uva, uint64 len)
{
  if (len > 0 && uva + len < uva)
    return 0;

  if (uva >= MAXVA)
    return 0;
  if (len > 0 && uva + len > MAXVA)
    return 0;

  uint64 sz = p->sz;

  if (uva >= sz)
    return 0;
  if (len > 0 && uva + len > sz)
    return 0;

  return 1;
}

// Copy to user or kernel address.
int
either_copyout(int user_dst, uint64 dst, void *src, uint64 len)
{
  struct proc *p = myproc();
  if (user_dst) {
    if (!uaddr_in_range(p, dst, len))
      return -1;
    return copyout(p->pagetable, dst, src, len);
  } else {
    memmove((char*)dst, src, len);
    return 0;
  }
}

// Copy from user or kernel address.
int
either_copyin(void *dst, int user_src, uint64 src, uint64 len)
{
  struct proc *p = myproc();
  if (user_src) {
    if (!uaddr_in_range(p, src, len))
      return -1;
    return copyin(p->pagetable, dst, src, len);
  } else {
    memmove(dst, (char*)src, len);
    return 0;
  }
}

// Print process list.
void
procdump(void)
{
  static char *states[] = {
  [UNUSED]    "unused",
  [USED]      "used",
  [SLEEPING]  "sleep ",
  [RUNNABLE]  "runble",
  [RUNNING]   "run   ",
  [ZOMBIE]    "zombie"
  };
  char *state;

  printf("\n");
  acquire(&proc_table_lock);
  for (struct proc *p = proc_table.next; p != &proc_table; p = p->next) {
    if (p->state == UNUSED)
      continue;
    if (p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    printf("%d %s %s", p->pid, state, p->name);
    printf("\n");
  }
  release(&proc_table_lock);
}
