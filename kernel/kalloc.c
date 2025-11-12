// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

void freerange(void *pa_start, void *pa_end);

extern char end[];
                  

void kinit() {
  char* p = (char*)PGROUNDUP((uint64)end);
  
  if (PGSIZE % 16 != 0) {
    panic("PGSIZE not multiple of LEAF_SIZE");
  }
  
  bd_init(p, (void*) PHYSTOP);
}
void
kfree_bd(void *p) { 
    bd_free(p);
}

void kfree(void *pa) {
    if(pa == 0) return;
    bd_free(pa);
}

void *kalloc(void) {
    void *pa = bd_malloc(PGSIZE);
    
    if(pa == 0) {
        #ifdef DEBUG
        printf("kalloc: out of memory\n");
        #endif
        return 0;
    }
    
    memset(pa, 0, PGSIZE);
    return pa;
}
