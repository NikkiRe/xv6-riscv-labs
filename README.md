# xv6-riscv Operating System Project

## Overview

This repository contains an implementation and exploration of **xv6-riscv**, a teaching operating system based on Unix v6 (Edition 6) that has been ported to run on the RISC-V architecture. xv6-riscv is a simplified OS designed for educational purposes, making it ideal for understanding core operating system concepts and their practical implementation.

## What is xv6-riscv?

**xv6** is a reimplementation of Unix v6, originally written in assembly for the PDP-11. **xv6-riscv** specifically targets the RISC-V instruction set architecture, a modern, open-source ISA that has gained significant traction in both academia and industry.

Key characteristics:
- **Educational Focus**: Designed to be readable and understandable for students learning OS concepts
- **Monolithic Kernel**: Single kernel space implementation without microkernel architecture
- **POSIX-like Interface**: Provides familiar Unix-like system calls and behaviors
- **Minimal Dependencies**: Self-contained with minimal external dependencies
- **Well-Documented**: Source code includes extensive comments explaining kernel operations

## Architecture

### RISC-V Target
The xv6-riscv implementation targets the RISC-V instruction set architecture:
- **ISA**: RV64 (64-bit RISC-V)
- **Boot Process**: SBI (Supervisor Binary Interface) based boot
- **Memory Model**: Virtual memory with page-based translation
- **Privilege Levels**: Uses supervisor mode for kernel execution

### Kernel Components

#### 1. **Boot and Initialization**
- Early boot code handles CPU initialization
- Sets up virtual memory mapping
- Initializes kernel data structures

#### 2. **Process Management**
- Process creation and destruction (`fork()`, `exit()`)
- Process scheduling using a simple round-robin scheduler
- Context switching between processes
- Process state management (runnable, sleeping, zombie)

#### 3. **Memory Management**
- Virtual memory implementation with page tables
- User and kernel address space separation
- Memory allocation and deallocation
- Copy-on-write optimization (in some variants)

#### 4. **File System**
- Simple on-disk file system
- Inode-based file organization
- Directory support with name resolution
- File operations (open, read, write, close, seek)

#### 5. **Synchronization and Concurrency**
- Spinlocks for mutual exclusion
- Sleep/wakeup mechanism for process synchronization
- Interrupt handling and management

#### 6. **System Call Interface**
- Process management: `fork()`, `exit()`, `wait()`, `kill()`, `getpid()`
- File I/O: `open()`, `close()`, `read()`, `write()`, `seek()`
- File system: `mkdir()`, `link()`, `unlink()`, `chdir()`
- Other: `exec()`, `pipe()`, `dup()`, `dup2()`

## Key Learning Outcomes

By studying xv6-riscv, you'll understand:

- **How operating systems work at a fundamental level**
  - Context switching and multi-tasking
  - Virtual memory and address translation
  - Interrupt and exception handling

- **Kernel architecture and design patterns**
  - Monolithic kernel structure
  - System call dispatch mechanisms
  - Kernel subsystem organization

- **Low-level programming concepts**
  - Assembly language for CPU-specific operations
  - Hardware-software interface
  - Memory protection and isolation

- **Classic OS algorithms**
  - Process scheduling
  - Page replacement strategies
  - Lock-free and locking mechanisms

## Project Structure

```
os-course1/
├── README.md                 # This file
├── kernel/
│   ├── asm/                 # RISC-V assembly code
│   ├── fs/                  # File system implementation
│   ├── proc/                # Process management
│   ├── mm/                  # Memory management
│   ├── include/             # Header files
│   └── main.c               # Kernel entry point
├── user/                     # User space programs
├── Makefile                  # Build configuration
├── tools/                    # Build and debug tools
└── documentation/            # Additional documentation
```

## Building and Running

### Prerequisites
- RISC-V GCC toolchain (`riscv64-unknown-elf-gcc`)
- QEMU emulator with RISC-V support (`qemu-system-riscv64`)
- Standard build tools (make, gcc, binutils)

### Build Instructions

```bash
# Clone the repository
git clone https://github.com/NikkiRe/os-course1.git
cd os-course1

# Build the kernel and user programs
make

# Run in QEMU
make qemu
```

### Debug with GDB

```bash
# Terminal 1: Run QEMU in debug mode
make qemu-gdb

# Terminal 2: Connect with GDB
riscv64-unknown-elf-gdb kernel/kernel
(gdb) target remote localhost:1234
(gdb) break main
(gdb) continue
```

## Core System Calls

### Process Management
- `int fork()` - Create a new process
- `int exit(int status)` - Terminate the calling process
- `int wait(int *status)` - Wait for child process
- `int exec(char *path, char *argv[])` - Execute a program
- `int getpid()` - Get process ID

### File I/O
- `int open(char *path, int flags)` - Open or create a file
- `int read(int fd, char *buf, int n)` - Read from file
- `int write(int fd, char *buf, int n)` - Write to file
- `int close(int fd)` - Close a file descriptor

### Synchronization
- `int sleep(int ticks)` - Sleep for specified ticks
- `int kill(int pid)` - Send signal to process

## Educational Value

This project is particularly valuable for:
- **Operating Systems Courses**: Practical supplement to theoretical study
- **Systems Programming**: Understanding hardware-software interaction
- **Computer Architecture**: Seeing architecture concepts in action
- **Competitive Programming**: Solving real systems challenges

## References and Resources

- **MIT 6.S081**: Operating System Engineering course (uses xv6)
- **RISC-V Specification**: https://riscv.org/specifications/
- **xv6 Book**: Available online with detailed explanations
- **QEMU Documentation**: https://www.qemu.org/
- **GCC RISC-V Toolchain**: https://github.com/riscv-collab/riscv-gnu-toolchain

## Contributing

Contributions are welcome! Whether it's bug fixes, improvements, additional documentation, or educational enhancements, please feel free to:

1. Fork the repository
2. Create a feature branch
3. Make your changes
4. Submit a pull request

## License

This project is typically under an open-source license. Please check the LICENSE file for specific details.

## Acknowledgments

- Original xv6 authors for the Unix-like OS design
- RISC-V ISA designers and community
- MIT for the 6.S081 course materials
- All contributors who have improved this implementation

## Getting Help

- **Issues**: Report bugs or ask questions via GitHub Issues
- **Documentation**: Check the `documentation/` directory for detailed guides
- **Community**: Engage with fellow learners in discussions

---

**Last Updated**: 2025-12-29

For questions or suggestions about this project, please open an issue on GitHub.
