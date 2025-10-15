#include "../kernel/types.h"
#include "user.h"
#include "../kernel/param.h"
#include "../kernel/spinlock.h"
#include "../kernel/riscv.h"
#include "../kernel/proc.h"


int main() {
    unsigned long ret;
    allocstat(1, (uint64)&ret);
    printf("%lu\n", ret);
}
