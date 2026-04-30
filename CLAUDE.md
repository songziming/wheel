# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build System

Wheel is built with LLVM (clang + ld.lld) using GNU Make. The `TOOLCHAIN` env var should point to the cross-compiler prefix.

On Windows, all build commands must run inside WSL:

```bash
wsl make              # build kernel ELF (build/wheel.elf)
wsl make iso          # create bootable ISO (build/cd.iso)
wsl make unit         # build unit test binary (build/unit)
wsl make cov          # run unit tests and generate HTML coverage report in build/coverage
wsl make clean        # remove build/ directory
```

`DEBUG=1` is the default; pass `DEBUG=0` for optimized builds (`-O2 -DNDEBUG`). `ARCH` defaults to `x86_64`.

Run the ISO under QEMU: `./run_iso.sh` (Linux/macOS) or `run_iso.bat` (Windows).

CI runs `make unit && build/unit` on every push (GitHub Actions, ubuntu-latest).

## Architecture Overview

**Wheel** is a 64-bit higher-half kernel for x86_64, written from scratch. Ring-0 only (no user mode), multiprocessor, preemptive scheduling, simple framebuffer console.

### Kernel source layout (`kernel/`)

| Directory | Purpose |
|-----------|---------|
| `arch_x86_64/` | X86_64-specific code: boot, ACPI, APIC (Local + IO), CPU setup, devices, MMU |
| `core/` | Architecture-independent OS primitives: task/scheduler, timer (ktimer), semaphore, work queue |
| `lib/` | Data structures and utilities: doubly-linked list, rbtree, spinlock, rwlock, kstring/format |
| `mem/` | Physical page allocator (buddy system), virtual address space (vmspace), early allocator |
| `debug/` | Debug support: ubsan, stack unwinding, benchmarks |

### Key virtual address space layout (`arch_x86_64/arch_config.h`)

- Kernel code: `0xFFFFFFFF80000000` (-2G, higher-half)
- Direct-map of all physical memory: `0xFFFF800000000000` (16 TB virtual)
- Kernel stacks zone: `0xFFFF900000000000` – `0xFFFFA00000000000`
- Physical load address: `0x100000` (1 MB)

### Architecture / arch API boundary

`kernel/core/arch_api_common.h` defines the interface that each architecture must implement:
- **SMP & per-CPU data**: `cpu_count()`, `cpu_index()`, `percpu_ptr()`, `thiscpu_ptr()` — x86_64 uses GS segment for fast per-CPU access via macros `THISCPU(x)` / `PERCPU(i,x)`
- **Interrupts**: `cpu_int_depth()`, `cpu_int_lock()`/`cpu_int_unlock()`
- **MMU**: `mmu_create()`/`mmu_delete()` for page tables, `mmu_map()`/`mmu_unmap()` for mappings
- **Task context switch**: `arch_task_init()`, `arch_task_switch()`

### Linker script sections (`layout.ld`)

The kernel uses custom sections with dedicated program headers: `boot`, `percpu`, `real`, `init`, `text`, `rodata`, `data`. `.init.text`/`.init.data`/`.init.bss` are freed after boot. `.percpu.data`/`.percpu.bss` hold per-CPU variable templates that get copied per-core.

### Scheduler design (`core/task.c`)

- 32 priority levels (0=highest, 31=lowest, reserved for idle)
- Per-CPU ready queue with bitmask for fast highest-priority lookup
- Round-robin within the same priority (tick-based time slice)
- Idle tasks run on each CPU; new tasks are placed on an idle CPU if one exists, otherwise on the current CPU
- `g_balance_lock` (rwspin) coordinates load balancing across CPUs

### Synchronization primitives (`lib/spin.h`)

- **spin_t**: ticket-based spinlock with `raw_spin_take`/`raw_spin_give` and IRQ-safe variants (`irq_spin_take` saves interrupt state)
- **rwspin_t**: reader-writer spinlock built on top of `spin_t` — allows concurrent readers, exclusive writers

### Physical memory (`mem/page.h`)

Buddy allocator with page tracking. Pages have types (`PT_FREE`, `PT_KERNEL`, `PT_PGTBL`, `PT_STACK`). Each page descriptor is a `page_t` with rank (order in the buddy system). `PAGE_ALLOC(rank, type)` is the common allocation macro.

## Unit Testing

Tests are written in C++ using Google Test (`libgtest-dev`), compiled with the host toolchain. Kernel C code is compiled separately as `-fPIC -DUNIT_TEST -DDEBUG` into `libwheel.so`, then linked with the test `.cc` files.

Key test files:
- `kernel/lib/*.test.cc` — dllist, kstring, format, rbtree
- `kernel/mem/page.test.cc` — physical page allocator
- `kernel/core/ktimer.test.cc` — timer subsystem
- `kernel/arch_x86_64/arch_mock.cc` / `kernel/mem/early_alloc.mock.cc` — mock implementations for unit tests

```bash
make unit && LD_LIBRARY_PATH=build build/unit   # build and run
make cov                                         # build + run + HTML coverage
```

## Source conventions

- `.c` files in `kernel/` are kernel code; `.cc` files are unit tests or mocks
- `INIT_TEXT` / `INIT_DATA` macros place code/data in init sections that are freed after boot
- `PERCPU_DATA` / `PERCPU_BSS` macros mark per-CPU variables
- `UNUSED`, `PACKED`, `NORETURN`, `PRINTF(s,a)` are defined in `core/wheel.h`
- Comments and identifiers are in Chinese (functions/variables: English snake_case; file names: English)
- Host tools live in `host/` (PSF font parser, BIOS roms, disk image helper)
