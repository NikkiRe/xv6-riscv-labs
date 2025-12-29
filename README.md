# xv6-riscv Labs (RISC-V)

## Overview

This repository contains a fork of **xv6-riscv**, a small Unix-like teaching operating system for the **RISC-V (RV64)** architecture, along with implementations for multiple course labs.

### Repository layout principle

- **main** — clean baseline xv6-riscv code used as the starting point for the course
- **lab-\*** branches — independent lab solutions (each lab has a different goal and a different set of changes)

If you are looking for a specific lab implementation, switch to the corresponding branch.

---

## Labs and branches

| Lab | Branch | Topic |
|---:|:---|:---|
| 1 | `lab-1` | Intro to xv6 & syscalls (pipes, `dump` / `dump2`) |
| 2 | `lab-2` | Memory allocator: buddy allocator integration, dynamic file allocation, allocator optimization |
| 3 | `lab-3` | Copy-on-write `fork` (page-fault handling, refcounting), optional lazy allocation |

Each branch is based on `main` and contains only the changes required for that lab.

### Switch to a lab branch

    git checkout lab-1
    # or lab-2 / lab-3

### Compare a lab against baseline

    git diff main..lab-2

---

## Lab details

This section briefly describes what is implemented in each lab branch. Full assignment texts may be provided separately by the course.

### Lab 1 — Introduction to xv6 (branch: `lab-1`)

Goal: get familiar with xv6 user space and system calls, then add new system calls.

What’s inside:

- **Pingpong**: a user-space program that demonstrates inter-process communication using **Unix pipes**.
  - Create a pipe, fork a child process, send `"ping"` parent → child, then send `"pong"` child → parent.
  - Print messages in the format `<pid>: got <message>`.
  - Practice using `pipe`, `fork`, `read`, `write`, `getpid`, and proper `exit(0)`.

- **dump syscall**: add a new system call `dump()` that prints the values of registers **s2–s11** for the calling process.
  - Read register values from the process trapframe (`myproc()->trapframe`).
  - Print only the lower 32 bits of each 64-bit register.
  - Wire it through user/kernel glue (`user.h`, `usys.pl`, `syscall.*`, `defs.h`, etc.).

- **dump2 syscall (optional / advanced)**: implement `dump2(pid, register_num, return_value)`:
  - Read a single register (s2–s11) from another process if permissions allow (only the process itself and its ancestors).
  - Return specific error codes:
    - `-1` no permission
    - `-2` pid does not exist
    - `-3` invalid register number
    - `-4` failed to write to user address (`copyout`)
  - Use syscall argument helpers and `copyout` to return data to user space.

How to validate (typical):
- Run user-level tests like `dumptests` / `dump2tests` if present in the branch.

---

### Lab 2 — Buddy allocator integration (branch: `lab-2`)

Goal: replace static allocation of “small kernel objects” with dynamic allocation using a **buddy allocator**, and optimize allocator metadata.

What’s inside:

- **Dynamic file structures**:
  - Replace the static `ftable.file[NFILE]` array with dynamic allocation.
  - Allocate file structures in `filealloc()` using `bd_malloc()`.
  - Free them in `fileclose()` using the corresponding buddy free function.
  - Revisit locking logic (why it exists and whether it changes with dynamic allocation).

- **Allocator initialization**:
  - Integrate buddy allocator initialization into the kernel allocator path (via `kalloc.c` changes calling buddy functions).

- **Allocator optimization**:
  - Optimize metadata by replacing a per-block “allocated” bit with an XOR scheme for buddy pairs (track whether exactly one of two buddies is allocated).
  - Ensure coalescing logic still works correctly.
  - Measure/observe increased free memory after the optimization.

How to validate (typical):
- Run `alloctest` (filetest + memtest) and `usertests` if present.

---

### Lab 3 — Copy-on-write fork (branch: `lab-3`)

Goal: implement **copy-on-write (COW)** for `fork()` so that parent/child initially share physical pages and only copy on first write.

What’s inside:

- **Shared mappings in fork**:
  - Modify the memory copy logic so `fork()` maps the same physical pages into the child (no eager copying).
  - Understand and fix the “what goes wrong” when pages are shared without protection.

- **vmprint**:
  - Add a `vmprint(pagetable_t)` function to print a process page table with a readable multi-level format.

- **Fault-on-write mechanism**:
  - Mark shared pages as **not writable** (clear `PTE_W`) for both parent and child.
  - Use RISC-V page faults (scause 13/15) to detect writes to COW pages.
  - Store a “COW” marker using available bits (e.g., RSW bits in PTE).

- **COW page fault handler**:
  - In `usertrap`, on a write fault to a COW-marked page:
    - Allocate a new physical page
    - Copy contents from the old page
    - Remap the virtual address to the new page with write permission
  - Maintain a **reference counter** for physical pages to free only when no processes use them.
  - If a page is COW-marked but the refcount is 1, just “unlock” it (restore write) without copying.

- **copyout support**:
  - Extend `copyout()` to also handle COW pages when kernel writes into user memory.

- **Optional / advanced**: lazy allocation via `sbrk()`
  - Make `sbrk` increase address space without allocating pages immediately.
  - Allocate on-demand in the page fault handler.
  - Handle edge cases and pass `lazytests` / `usertests` if present.

How to validate (typical):
- Run `cowtest` and `usertests` (and `lazytests` if you implement the optional part).

---

## What is xv6-riscv?

**xv6** is a reimplementation of Unix v6, originally written in assembly for the PDP-11. **xv6-riscv** specifically targets the RISC-V instruction set architecture, a modern, open-source ISA widely used in academia and industry.

### Key characteristics

- **Educational Focus**: designed to be readable and understandable for students learning OS concepts
- **Monolithic Kernel**: single kernel space implementation (no microkernel architecture)
- **POSIX-like Interface**: provides familiar Unix-like system calls and behaviors
- **Minimal Dependencies**: self-contained with minimal external dependencies
- **Well-Documented**: source code includes extensive comments explaining kernel operations

---

## Architecture

### RISC-V Target

- **ISA**: RV64 (64-bit RISC-V)
- **Boot Process**: SBI (Supervisor Binary Interface) based boot
- **Memory Model**: virtual memory with page-based translation
- **Privilege Levels**: uses supervisor mode for kernel execution

### Kernel components

#### 1. Boot and initialization

- Early boot code handles CPU initialization
- Sets up virtual memory mapping
- Initializes kernel data structures

#### 2. Process management

- Process creation and destruction (`fork()`, `exit()`)
- Process scheduling using a simple round-robin scheduler
- Context switching between processes
- Process state management (runnable, sleeping, zombie)

#### 3. Memory management

- Virtual memory implementation with page tables
- User and kernel address space separation
- Memory allocation and deallocation
- Copy-on-write optimization (implemented in `lab-3`)

#### 4. File system

- Simple on-disk file system
- Inode-based file organization
- Directory support with name resolution
- File operations (open, read, write, close, seek)

#### 5. Synchronization and concurrency

- Spinlocks for mutual exclusion
- Sleep/wakeup mechanism for process synchronization
- Interrupt handling and management

#### 6. System call interface

- Process management: `fork()`, `exit()`, `wait()`, `kill()`, `getpid()`
- File I/O: `open()`, `close()`, `read()`, `write()`, `seek()`
- File system: `mkdir()`, `link()`, `unlink()`, `chdir()`
- Other: `exec()`, `pipe()`, `dup()`, `dup2()`

---

## Key learning outcomes

By studying xv6-riscv and completing these labs, you will understand:

- **How operating systems work at a fundamental level**
  - context switching and multi-tasking
  - virtual memory and address translation
  - interrupt and exception handling

- **Kernel architecture and design patterns**
  - monolithic kernel structure
  - system call dispatch mechanisms
  - kernel subsystem organization

- **Low-level programming concepts**
  - assembly language for CPU-specific operations
  - hardware-software interface
  - memory protection and isolation

- **Classic OS topics and techniques**
  - process scheduling
  - memory allocators and accounting
  - copy-on-write and page-fault driven mechanisms
  - locking and synchronization

---

## Project structure

Exact directories may vary slightly between branches. The baseline structure is:

    .
    ├── README.md
    ├── kernel/
    ├── user/
    ├── Makefile
    └── (other xv6-riscv files)

---

## Building and running

### Prerequisites

- RISC-V GCC toolchain (`riscv64-unknown-elf-gcc`)
- QEMU emulator with RISC-V support (`qemu-system-riscv64`)
- Standard build tools (`make`, `gcc`, `binutils`)

### Build

    make

### Run in QEMU

    make qemu

### Debug with GDB

Terminal 1:

    make qemu-gdb

Terminal 2:

    riscv64-unknown-elf-gdb kernel/kernel
    (gdb) target remote localhost:1234
    (gdb) break main
    (gdb) continue

---

## References and resources

- **MIT 6.S081**: Operating System Engineering course (xv6-based)
- **RISC-V Specification**: https://riscv.org/specifications/
- **xv6 book**: *xv6: a simple, Unix-like teaching operating system*
- **QEMU Documentation**: https://www.qemu.org/
- **RISC-V GNU Toolchain**: https://github.com/riscv-collab/riscv-gnu-toolchain

---

## Acknowledgments

- Original xv6 authors for the Unix-like OS design
- RISC-V ISA designers and community
- MIT for the 6.S081 course materials
- All contributors who have improved this implementation

---

## Getting help

- **Issues**: report bugs or ask questions via GitHub Issues
- **Documentation**: check the `documentation/` directory for detailed guides (if present in your branch)
- **Community**: engage with fellow learners in discussions
