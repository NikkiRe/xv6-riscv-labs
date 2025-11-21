#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

void freerange(void *pa_start, void *pa_end);

extern char end[];

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
} kmem;

#define PA2IDX(pa) (((uint64)(pa)) / PGSIZE)
#define MAX_PAGES (PHYSTOP / PGSIZE)

struct {
  int refcnt[MAX_PAGES];
} pageref;

void
kinit()
{
  initlock(&kmem.lock, "kmem");
  
  for(int i = 0; i < MAX_PAGES; i++) {
    pageref.refcnt[i] = 1;
  }
  
  freerange(end, (void*)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
    kfree(p);
}

void
kfree(void *pa)
{
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  int idx = PA2IDX(pa);
  int ref = __sync_sub_and_fetch(&pageref.refcnt[idx], 1);
  
  // Fast path: still referenced
  if(ref > 0)
    return;
  
  // Error check
  if(ref < 0)
    panic("kfree: negative refcount");

  // Free the page
  memset(pa, 1, PGSIZE);
  r = (struct run*)pa;

  acquire(&kmem.lock);
  r->next = kmem.freelist;
  kmem.freelist = r;
  release(&kmem.lock);
}

void *
kalloc(void)
{
  struct run *r;

  acquire(&kmem.lock);
  r = kmem.freelist;
  if(r)
    kmem.freelist = r->next;
  release(&kmem.lock);

  if(r){
    pageref.refcnt[PA2IDX(r)] = 1;  // Non-atomic OK: we just allocated it
  }
  return (void*)r;
}

void
krefpage(void *pa)
{
  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    return;
  
  int idx = PA2IDX(pa);
  __sync_fetch_and_add(&pageref.refcnt[idx], 1);
}

int
krefcnt(void *pa)
{
  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    return 0;
  
  int cnt = pageref.refcnt[PA2IDX(pa)];
  return cnt;
}
