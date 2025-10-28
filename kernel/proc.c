#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"

struct cpu cpus[NCPU];

struct proc *initproc;
struct proc proc_table;

#define MAXPROC 64

static struct {
  struct spinlock lock;
  int count;
} proccnt;

static void proc_table_init(struct proc* p) {
  p->pid = -1;
  p->state = UNUSED;
  p->next = p;
  p->last = p;
}

static void proc_table_push(struct proc* head, struct proc* p) {
  p->next = head->next;
  p->last = head;
  head->next->last = p;
  head->next = p;
}

static void proc_table_remove(struct proc* p) {
  p->last->next = p->next;
  p->next->last = p->last;
  p->next = 0;
  p->last = 0;
}

int nextpid = 1;
struct spinlock pid_lock;
extern void forkret(void);
static void freeproc(struct proc *p);

extern char trampoline[];

struct spinlock proc_table_lock;

void
proc_mapstacks(pagetable_t kpgtbl)
{
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

int
cpuid()
{
  int id = r_tp();
  return id;
}

struct cpu*
mycpu(void)
{
  int id = cpuid();
  struct cpu *c = &cpus[id];
  return c;
}

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
allocpid()
{
  int pid;
  acquire(&pid_lock);
  pid = nextpid;
  nextpid = nextpid + 1;
  release(&pid_lock);
  return pid;
}

static struct proc*
allocproc(void)
{
  acquire(&proccnt.lock);
  if (proccnt.count >= MAXPROC) {
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
  p->pid = allocpid();
  p->state = USED;

  if ((p->kstack = (uint64) kalloc()) == 0) {
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

  memset(&p->context, 0, sizeof(p->context));
  p->context.ra = (uint64)forkret;
  p->context.sp = p->kstack + PGSIZE;

  return p;
}

static void
freeproc(struct proc *p)
{
  if (p->kstack) {
    kfree((void *) p->kstack);
    p->kstack = 0;
  }

  if(p->trapframe){
    kfree((void*)p->trapframe);
    p->trapframe = 0;
  }

  if(p->pagetable){
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

  if (p->next && p->last) {
    proc_table_remove(p);
  }

  bd_free((void *)p);

  acquire(&proccnt.lock);
  if (proccnt.count > 0)
    proccnt.count--;
  release(&proccnt.lock);
}

pagetable_t
proc_pagetable(struct proc *p)
{
  pagetable_t pagetable;

  pagetable = uvmcreate();
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

void
proc_freepagetable(pagetable_t pagetable, uint64 sz)
{
  uvmunmap(pagetable, TRAMPOLINE, 1, 0);
  uvmunmap(pagetable, TRAPFRAME, 1, 0);
  uvmfree(pagetable, sz);
}

uchar initcode[] = {
  0x17, 0x05, 0x00, 0x00, 0x13, 0x05, 0x45, 0x02,
  0x97, 0x05, 0x00, 0x00, 0x93, 0x85, 0x35, 0x02,
  0x93, 0x08, 0x70, 0x00, 0x73, 0x00, 0x00, 0x00,
  0x93, 0x08, 0x20, 0x00, 0x73, 0x00, 0x00, 0x00,
  0xef, 0xf0, 0x9f, 0xff, 0x2f, 0x69, 0x6e, 0x69,
  0x74, 0x00, 0x00, 0x24, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00
};

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
  np->trapframe->a0 = 0;

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

void
wakeup_holding_proc_table_lock(void* chan) {
  struct proc *self = myproc();
  for (struct proc* p = proc_table.next; p != &proc_table; p = p->next) {
    if (p != self && p->state == SLEEPING && p->chan == chan) {
      p->state = RUNNABLE;
    }
  }
}

void
reparent(struct proc *p)
{
  struct proc *pp;
  struct proc *table_i;
  for (table_i = proc_table.next; table_i != &proc_table; table_i = table_i->next) {
    pp = table_i;
    if (pp->parent == p) {
      pp->parent = initproc;
      wakeup_holding_proc_table_lock(initproc);
    }
  }
}

void
exit(int status) {
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

int
wait(uint64 addr)
{
  struct proc *pp;
  int havekids, pid;
  struct proc *p = myproc();

  acquire(&proc_table_lock);

  for(;;){
    havekids = 0;
    struct proc *table_i;
    for (table_i = proc_table.next; table_i != &proc_table; table_i = table_i->next) {
      pp = table_i;
      if (pp->parent == p) {
        havekids = 1;
        if (pp->state == ZOMBIE) {
          pid = pp->pid;
          if (addr != 0 && copyout(p->pagetable, addr, (char *) &pp->xstate,
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

    if (!havekids || p->killed) {
      release(&proc_table_lock);
      return -1;
    }
    sleep(p, &proc_table_lock);
  }
}

void
scheduler(void) {
  struct proc *p;
  struct cpu *c = mycpu();

  c->proc = 0;
  for (;;) {
    intr_on();
    acquire(&proc_table_lock);
    struct proc *table_i;
    for (table_i = proc_table.next; table_i != &proc_table; table_i = table_i->next) {
      p = table_i;
      if (p->state == RUNNABLE) {
        p->state = RUNNING;
        c->proc = p;
        swtch(&c->context, &p->context);
        c->proc = 0;
      }
    }
    release(&proc_table_lock);
  }
}

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

void
yield(void)
{
  struct proc *p = myproc();
  acquire(&proc_table_lock);
  p->state = RUNNABLE;
  sched();
  release(&proc_table_lock);
}

void
forkret(void)
{
  static int first = 1;

  release(&proc_table_lock);

  if (first) {
    first = 0;
    fsinit(ROOTDEV);
  }

  usertrapret();
}

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

void
wakeup(void *chan)
{
  acquire(&proc_table_lock);
  wakeup_holding_proc_table_lock(chan);
  release(&proc_table_lock);
}

int
kill(int pid)
{
  struct proc *p;
  acquire(&proc_table_lock);
  for(p = proc_table.next; p != &proc_table; p = p->next) {
    if(p->pid == pid){
      p->killed = 1;
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
  acquire(&proc_table_lock);
  p->killed = 1;
  release(&proc_table_lock);
}

int
killed(struct proc *p)
{
  int k;
  acquire(&proc_table_lock);
  k = p->killed;
  release(&proc_table_lock);
  return k;
}

int
either_copyout(int user_dst, uint64 dst, void *src, uint64 len)
{
  struct proc *p = myproc();
  if(user_dst){
    return copyout(p->pagetable, dst, src, len);
  } else {
    memmove((char *)dst, src, len);
    return 0;
  }
}

int
either_copyin(void *dst, int user_src, uint64 src, uint64 len)
{
  struct proc *p = myproc();
  if(user_src){
    return copyin(p->pagetable, dst, src, len);
  } else {
    memmove(dst, (char*)src, len);
    return 0;
  }
}

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
  struct proc *p;
  char *state;

  printf("\n");
  struct proc *table_i;
  for (table_i = proc_table.next; table_i != &proc_table; table_i = table_i->next) {
    p = table_i;
    if (p->state == UNUSED)
      continue;
    if (p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    printf("%d %s %s", p->pid, state, p->name);
    printf("\n");
  }
}