#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"

struct cpu cpus[NCPU];

struct proc *initproc;

// Head of doubly-linked circular list of processes.
struct proc proc_table;

// Counter of active process descriptors (for NPROC limit).
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

// Global lock for process list.
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

// Must be called with interrupts disabled.
int
cpuid()
{
  int id = r_tp();
  return id;
}

// Return pointer to current CPU.
// Interrupts must be disabled.
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

// Allocate and partially initialize new process descriptor.
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
  initlock(&p->lock, "proc"); // for compatibility with holding() checks
  p->pid = allocpid();
  p->state = USED;

  // kernel stack
  if ((p->kstack = (uint64)kalloc()) == 0) {
    freeproc(p);
    return 0;
  }

  // trapframe
  if((p->trapframe = (struct trapframe *)kalloc()) == 0){
    freeproc(p);
    return 0;
  }

  // user page table
  p->pagetable = proc_pagetable(p);
  if(p->pagetable == 0){
    freeproc(p);
    return 0;
  }

  // Initial context: return to forkret, SP at top of kstack.
  memset(&p->context, 0, sizeof(p->context));
  p->context.ra = (uint64)forkret;
  p->context.sp = p->kstack + PGSIZE;

  return p;
}

// Free all process resources and the descriptor itself.
// NOTE: may be called with or without proc_table_lock held.
// List removal is done to avoid reentrant acquire().
static void
freeproc(struct proc *p)
{
  // If process is in list, remove it while respecting lock state.
  if (p->next && p->last) {
    int have = holding(&proc_table_lock);
    if (!have)
      acquire(&proc_table_lock);
    // Re-check under lock: race condition possible.
    if (p->next && p->last)
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

// Create empty user pagetable with TRAMPOLINE/TRAPFRAME.
pagetable_t
proc_pagetable(struct proc *p)
{
  pagetable_t pagetable = uvmcreate();
  if(pagetable == 0)
    return 0;

  // trampoline (без PTE_U)
  if(mappages(pagetable, TRAMPOLINE, PGSIZE,
              (uint64)trampoline, PTE_R | PTE_X) < 0){
    uvmfree(pagetable, 0);
    return 0;
  }

  // trapframe (без PTE_U)
  if(mappages(pagetable, TRAPFRAME, PGSIZE,
              (uint64)(p->trapframe), PTE_R | PTE_W) < 0){
    uvmunmap(pagetable, TRAMPOLINE, 1, 0);
    uvmfree(pagetable, 0);
    return 0;
  }

  return pagetable;
}

// Free user pagetable and process memory.
void
proc_freepagetable(pagetable_t pagetable, uint64 sz)
{
  uvmunmap(pagetable, TRAMPOLINE, 1, 0);
  uvmunmap(pagetable, TRAPFRAME, 1, 0);
  uvmfree(pagetable, sz);
}

// initcode.S (as in xv6)
uchar initcode[] = {
  0x17, 0x05, 0x00, 0x00, 0x13, 0x05, 0x45, 0x02,
  0x97, 0x05, 0x00, 0x00, 0x93, 0x85, 0x35, 0x02,
  0x93, 0x08, 0x70, 0x00, 0x73, 0x00, 0x00, 0x00,
  0x93, 0x08, 0x20, 0x00, 0x73, 0x00, 0x00, 0x00,
  0xef, 0xf0, 0x9f, 0xff, 0x2f, 0x69, 0x6e, 0x69,
  0x74, 0x00, 0x00, 0x24, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00
};

// First process.
void
userinit(void)
{
  struct proc *p;

  p = allocproc();
  if (p == 0)
    panic("userinit: allocproc failed");

  initproc = p;

  // One user page and copy of initcode.
  uvmfirst(p->pagetable, initcode, sizeof(initcode));
  p->sz = PGSIZE;

  // Prepare "return" to user.
  p->trapframe->epc = 0;
  p->trapframe->sp = PGSIZE;

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");

  acquire(&proc_table_lock);
  proc_table_push(&proc_table, p);
  p->state = RUNNABLE;
  release(&proc_table_lock);
}

// Grow or shrink process memory by n bytes.
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

// Create child process (copy of parent).
int
fork(void)
{
  int i, pid;
  struct proc *np;
  struct proc *p = myproc();

  if((np = allocproc()) == 0){
    return -1;
  }

  // Copy user memory.
  if(uvmcopy(p->pagetable, np->pagetable, p->sz) < 0){
    freeproc(np);
    return -1;
  }
  np->sz = p->sz;

  // Copy registers.
  *(np->trapframe) = *(p->trapframe);
  // In child, fork() returns 0.
  np->trapframe->a0 = 0;

  // Duplicate open files.
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

// Wakeup while holding proc_table_lock.
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

// Give orphaned children to init. Must hold proc_table_lock.
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

  // Close files.
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

  // Give children to init, wake up parent.
  reparent(p);
  wakeup_holding_proc_table_lock(p->parent);

  p->xstate = status;
  p->state = ZOMBIE;

  // Switch to scheduler, never return.
  sched();
  panic("zombie exit");
}

// Wait for child to exit, return pid, or -1.
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
          // freeproc() will remove from list (without re-acquire)
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

    // Sleep until one of children exits.
    sleep(p, &proc_table_lock);
  }
}

// Scheduler: pick RUNNABLE process and switch to it.
// If none found, go to wfi until interrupt.
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
    // Stop at first RUNNABLE process for optimization
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

// Switch to scheduler. Must hold only proc_table_lock,
// and process state already changed (not RUNNING).
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

// Give up CPU for one scheduling round.
void
yield(void)
{
  struct proc *p = myproc();
  acquire(&proc_table_lock);
  p->state = RUNNABLE;
  sched();
  release(&proc_table_lock);
}

// First run of child after fork() comes here.
void
forkret(void)
{
  static int first = 1;

  // Entered from scheduler() under proc_table_lock — release it.
  release(&proc_table_lock);

  if (first) {
    first = 0;
    __sync_synchronize();
    fsinit(ROOTDEV);
  }

  usertrapret();
}

// Atomically release lock and sleep on chan.
// Reacquire original lock when awakened.
void
sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();

  if(lk != &proc_table_lock) {
    acquire(&proc_table_lock);
    release(lk);
  }

  // Go to sleep.
  p->chan = chan;
  p->state = SLEEPING;

  // Switch to scheduler. Hold proc_table_lock.
  sched();

  // Awakened — clean up.
  p->chan = 0;

  if(lk != &proc_table_lock) {
    release(&proc_table_lock);
    acquire(lk);
  }
}

// Wake up all processes sleeping on chan.
void
wakeup(void *chan)
{
  acquire(&proc_table_lock);
  wakeup_holding_proc_table_lock(chan);
  release(&proc_table_lock);
}

// Kill process by pid.
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

// Fast check of user space addresses against p->sz and MAXVA.
// Instantly rejects obviously invalid pointers (e.g., 0xffffffff),
// without expensive page table walks.
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

// Copy to either user or kernel address.
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

// Copy from either user or kernel address.
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

// Debug print of processes.
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
