#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
#include "dynamic_array.h"

struct cpu cpus[NCPU];

// Dynamic array of process pointers
static struct dynamic_array procs;
// Free list of indices for UNUSED procs
static struct dynamic_array free_ix;

// Per-CPU run queues
struct runq {
  struct proc *head;
  struct proc *tail;
  struct spinlock lock;
};
static struct runq rq[NCPU];

// PID hash table for fast lookup
#define PIDH 256
static struct proc* pidtab[PIDH];

struct proc *initproc;

int nextpid = 1;
struct spinlock pid_lock;
struct spinlock wait_lock;

extern void forkret(void);
static void freeproc(struct proc *p);

extern char trampoline[]; // trampoline.S

static inline struct proc **procs_ptr(void) {
  return (struct proc**)procs.data;
}

static inline size_t *free_ix_ptr(void) {
  return (size_t*)free_ix.data;
}

static int free_ix_pop(size_t *out) {
  if (free_ix.size == 0) return -1;
  *out = free_ix_ptr()[--free_ix.size];
  return 0;
}

static void free_ix_push(size_t i) {
  push_to_dynamic_array(&free_ix, (const char*)&i);
}

static inline unsigned phash(int pid) {
  return (unsigned)pid & (PIDH - 1);
}

static void pid_add(struct proc *p) {
  pidtab[phash(p->pid)] = p;
}

static void pid_del(struct proc *p) {
  if (pidtab[phash(p->pid)] == p) {
    pidtab[phash(p->pid)] = 0;
  }
}

static inline void rq_push(struct runq *q, struct proc *p) {
  p->rq_next = 0;
  if (!q->head) {
    q->head = p;
    q->tail = p;
  } else {
    q->tail->rq_next = p;
    q->tail = p;
  }
}

static inline struct proc* rq_pop(struct runq *q) {
  struct proc *p = q->head;
  if (!p) return 0;
  q->head = p->rq_next;
  if (!q->head) q->tail = 0;
  return p;
}

static inline int pickcpu(void) {
  return cpuid() % NCPU;
}

static void make_runnable(struct proc *p) {
  int c = pickcpu();
  acquire(&rq[c].lock);
  rq_push(&rq[c], p);
  release(&rq[c].lock);
}

// initialize the proc table to dynamic array of pointers.
void
procinit(void)
{
  initlock(&pid_lock, "nextpid");
  initlock(&wait_lock, "wait_lock");

  // Reserve space upfront for better performance
  if (create_dynamic_array(&procs, NPROC, sizeof(struct proc*)) != 0)
    panic("procinit: create_dynamic_array procs failed");
  if (create_dynamic_array(&free_ix, NPROC, sizeof(size_t)) != 0)
    panic("procinit: create_dynamic_array free_ix failed");
  
  for (int i = 0; i < NCPU; i++) {
    initlock(&rq[i].lock, "runq");
    rq[i].head = 0;
    rq[i].tail = 0;
  }
  
  for (int i = 0; i < PIDH; i++) {
    pidtab[i] = 0;
  }
}

// Must be called with interrupts disabled,
// to prevent race with process being moved
// to a different CPU.
int
cpuid()
{
  int id = r_tp();
  return id;
}

// Return this CPU's cpu struct.
struct cpu*
mycpu(void)
{
  int id = cpuid();
  struct cpu *c = &cpus[id];
  return c;
}

// Return the current struct proc *, or zero if none.
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

// Allocate index or append new one
static int
alloc_index_or_append(size_t *out)
{
  if (free_ix_pop(out) == 0) return 0;
  *out = procs.size;
  struct proc *nil = 0;
  if (push_to_dynamic_array(&procs, (const char*)&nil) != 0) return -1;
  return 0;
}

// Allocate new process slot at given index
static struct proc*
alloc_new_proc_slot_idx(size_t idx)
{
  struct proc *p = (struct proc*)bd_malloc(sizeof(struct proc));
  if (!p) return 0;
  memset(p, 0, sizeof(*p));
  initlock(&p->lock, "proc");
  p->state = UNUSED;
  p->kstack = 0;
  p->trapframe = 0;
  p->pagetable = 0;
  p->child_head = 0;
  p->sibling_next = 0;
  p->rq_next = 0;
  procs_ptr()[idx] = p;
  return p;
}

// Find an UNUSED proc among existing ones, or create a new one
static struct proc*
grab_unused_or_create(void)
{
  // Try to get index from free list first
  size_t idx;
  if (free_ix_pop(&idx) == 0) {
    struct proc **pp = procs_ptr();
    if (idx < procs.size) {
      struct proc *p = pp[idx];
      if (p) {
        acquire(&p->lock);
        if (p->state == UNUSED) {
          return p; // lock held, as in original xv6 allocproc()
        }
        release(&p->lock);
      } else {
        // Slot is empty, can allocate new proc here
        struct proc *p = alloc_new_proc_slot_idx(idx);
        if (p) {
          acquire(&p->lock);
          p->state = UNUSED;
          return p;
        }
      }
    }
    // Invalid or non-UNUSED, continue to check limit
  }
  
  // No free slot found - check NPROC limit before creating new
  struct proc **pp = procs_ptr();
  size_t active_count = 0;
  for (size_t i = 0; i < procs.size; i++) {
    struct proc *proc = pp[i];
    if (!proc) continue;
    acquire(&proc->lock);
    if (proc->state != UNUSED) active_count++;
    release(&proc->lock);
  }
  
  if (active_count >= NPROC) {
    return 0;
  }
  
  // Allocate new index
  if (alloc_index_or_append(&idx) != 0) return 0;
  
  struct proc *p = alloc_new_proc_slot_idx(idx);
  if (!p) {
    free_ix_push(idx);
    return 0;
  }
  acquire(&p->lock);
  p->state = UNUSED;
  return p;
}

// Mark index as free when process becomes UNUSED
static void
mark_index_free(struct proc *p)
{
  struct proc **pp = procs_ptr();
  for (size_t i = 0; i < procs.size; i++) {
    if (pp[i] == p) {
      free_ix_push(i);
      return;
    }
  }
}

// Look for an UNUSED proc and prepare it.
static struct proc*
allocproc(void)
{
  struct proc *p = grab_unused_or_create();
  if (!p) return 0;

  // Initialize process structure
  p->pid = allocpid();
  p->state = USED;
  pid_add(p);

  // Allocate kernel stack dynamically
  if((p->kstack = (uint64)kalloc()) == 0){
    pid_del(p);
    freeproc(p);
    release(&p->lock);
    return 0;
  }

  // Allocate a trapframe page dynamically
  if((p->trapframe = (struct trapframe *)kalloc()) == 0){
    pid_del(p);
    freeproc(p);
    release(&p->lock);
    return 0;
  }

  // Create an empty user page table
  if((p->pagetable = proc_pagetable(p)) == 0){
    pid_del(p);
    freeproc(p);
    release(&p->lock);
    return 0;
  }

  // Set up new context to start executing at forkret
  memset(&p->context, 0, sizeof(p->context));
  p->context.ra = (uint64)forkret;
  p->context.sp = p->kstack + PGSIZE;

  return p;
}

// free a proc structure and the data hanging from it (but keep struct proc for reuse).
// p->lock must be held.
static void
freeproc(struct proc *p)
{
  if(p->kstack)
    kfree((void*)p->kstack);
  p->kstack = 0;

  if(p->trapframe)
    kfree((void*)p->trapframe);
  p->trapframe = 0;

  if(p->pagetable)
    proc_freepagetable(p->pagetable, p->sz);
  p->pagetable = 0;

  p->sz = 0;
  p->pid = 0;
  p->parent = 0;
  p->child_head = 0;
  p->sibling_next = 0;
  p->rq_next = 0;
  p->name[0] = 0;
  p->chan = 0;
  p->killed = 0;
  p->xstate = 0;
  p->state = UNUSED;
}

// Create a user page table for a given process, with no user memory,
// but with trampoline and trapframe pages.
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

// a user program that calls exec("/init")
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
  if (!p) panic("userinit: allocproc");
  initproc = p;
  p->child_head = 0;
  p->sibling_next = 0;

  uvmfirst(p->pagetable, initcode, sizeof(initcode));
  p->sz = PGSIZE;

  p->trapframe->epc = 0;
  p->trapframe->sp = PGSIZE;

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");

  p->state = RUNNABLE;
  make_runnable(p);

  release(&p->lock);
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
    pid_del(np);
    freeproc(np);
    release(&np->lock);
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
  np->child_head = 0;
  np->sibling_next = 0;

  release(&np->lock);

  acquire(&wait_lock);
  np->parent = p;
  np->sibling_next = p->child_head;
  p->child_head = np;
  release(&wait_lock);

  acquire(&np->lock);
  np->state = RUNNABLE;
  make_runnable(np);
  release(&np->lock);

  return pid;
}

// Pass p's abandoned children to init.
// Caller must hold wait_lock.
void
reparent(struct proc *p)
{
  int need_wakeup = 0;
  
  for (struct proc *ch = p->child_head; ch; ) {
    struct proc *nxt = ch->sibling_next;
    acquire(&ch->lock);
    if (ch->parent == p) {
      ch->parent = initproc;
    }
    release(&ch->lock);
    ch->sibling_next = initproc->child_head;
    initproc->child_head = ch;
    ch = nxt;
    need_wakeup = 1;
  }
  p->child_head = 0;
  
  if(need_wakeup)
    wakeup(initproc);
}

void
exit(int status)
{
  struct proc *p = myproc();

  if(p == initproc)
    panic("init exiting");

  for(int fd = 0; fd < NOFILE; fd++){
    if(p->ofile[fd]){
      struct file *f = p->ofile[fd];
      fileclose(f);
      p->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(p->cwd);
  end_op();
  p->cwd = 0;

  acquire(&wait_lock);
  reparent(p);
  wakeup(p->parent);

  acquire(&p->lock);
  p->xstate = status;
  p->state = ZOMBIE;
  release(&wait_lock);

  sched();
  panic("zombie exit");
}

int
wait(uint64 addr)
{
  int havekids, pid;
  struct proc *p = myproc();

  acquire(&wait_lock);

  for(;;){
    havekids = 0;
    for (struct proc *ch = p->child_head; ch; ) {
      struct proc *nxt = ch->sibling_next;
      acquire(&ch->lock);
      if(ch->parent == p){
        havekids = 1;
        if(ch->state == ZOMBIE){
          pid = ch->pid;
          if(addr != 0 && copyout(p->pagetable, addr, (char *)&ch->xstate,
                                  sizeof(ch->xstate)) < 0) {
            release(&ch->lock);
            release(&wait_lock);
            return -1;
          }
          // Remove from child list
          if (p->child_head == ch) {
            p->child_head = ch->sibling_next;
          } else {
            struct proc *prev = p->child_head;
            while (prev && prev->sibling_next != ch) {
              prev = prev->sibling_next;
            }
            if (prev) prev->sibling_next = ch->sibling_next;
          }
          pid_del(ch);
          freeproc(ch);
          mark_index_free(ch);
          release(&ch->lock);
          release(&wait_lock);
          return pid;
        }
      }
      release(&ch->lock);
      ch = nxt;
    }
    if(!havekids || killed(p)){
      release(&wait_lock);
      return -1;
    }
    sleep(p, &wait_lock);
  }
}

// Per-CPU process scheduler.
void
scheduler(void)
{
  struct cpu *c = mycpu();
  c->proc = 0;

  for(;;){
    intr_on();

    int id = cpuid();
    acquire(&rq[id].lock);
    struct proc *p = rq_pop(&rq[id]);
    release(&rq[id].lock);
    
    if (!p) {
      intr_on();
      asm volatile("wfi");
      continue;
    }

    acquire(&p->lock);
    if(p->state == RUNNABLE) {
      p->state = RUNNING;
      c->proc = p;
      swtch(&c->context, &p->context);
      c->proc = 0;
    }
    release(&p->lock);
  }
}

void
sched(void)
{
  int intena;
  struct proc *p = myproc();

  if(!holding(&p->lock))
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
  acquire(&p->lock);
  p->state = RUNNABLE;
  make_runnable(p);
  sched();
  release(&p->lock);
}

void
forkret(void)
{
  static int first = 1;

  release(&myproc()->lock);

  if (first) {
    fsinit(ROOTDEV);
    first = 0;
    __sync_synchronize();
  }

  usertrapret();
}

void
sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();

  acquire(&p->lock);
  release(lk);

  p->chan = chan;
  p->state = SLEEPING;

  sched();

  p->chan = 0;

  release(&p->lock);
  acquire(lk);
}

void
wakeup(void *chan)
{
  struct proc **pp = procs_ptr();
  for (size_t i = 0; i < procs.size; i++) {
    struct proc *p = pp[i];
    if (!p) continue;
    acquire(&p->lock);
    if(p->state == SLEEPING && p->chan == chan) {
      p->state = RUNNABLE;
      make_runnable(p);
    }
    release(&p->lock);
  }
}

int
kill(int pid)
{
  struct proc *p = pidtab[phash(pid)];
  if (p) {
    acquire(&p->lock);
    if(p->pid == pid){
      p->killed = 1;
      if(p->state == SLEEPING){
        p->state = RUNNABLE;
        make_runnable(p);
      }
      release(&p->lock);
      return 0;
    }
    release(&p->lock);
  }
  // Fallback: scan all procs if hash miss
  struct proc **pp = procs_ptr();
  for (size_t i = 0; i < procs.size; i++) {
    p = pp[i];
    if (!p) continue;
    acquire(&p->lock);
    if(p->pid == pid){
      p->killed = 1;
      if(p->state == SLEEPING){
        p->state = RUNNABLE;
        make_runnable(p);
      }
      release(&p->lock);
      return 0;
    }
    release(&p->lock);
  }
  return -1;
}

void
setkilled(struct proc *p)
{
  acquire(&p->lock);
  p->killed = 1;
  release(&p->lock);
}

int
killed(struct proc *p)
{
  int k;
  acquire(&p->lock);
  k = p->killed;
  release(&p->lock);
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

  printf("\n");
  struct proc **pp = procs_ptr();
  for (size_t i = 0; i < procs.size; i++) {
    struct proc *p = pp[i];
    if (!p || p->state == UNUSED) continue;
    char *state;
    if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    printf("%d %s %s\n", p->pid, state, p->name);
  }
}
