# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build System

Wheel is built with LLVM (clang + ld.lld) using GNU Make. The `TOOLCHAIN` env var should point to the cross-compiler prefix.

On Windows, all build commands must run inside WSL:

```bash
wsl make              # build kernel ELF + embedded user programs (build/wheel.elf)
wsl make iso          # create bootable ISO (build/cd.iso)
wsl make kunit        # build unit test binary (build/unit)
wsl make kcov         # run unit tests and generate HTML coverage report in build/coverage
wsl make clean        # remove build/ directory
```

`DEBUG=1` is the default; pass `DEBUG=0` for optimized builds (`-O2 -DNDEBUG`). `ARCH` defaults to `x86_64`.

Run the ISO under QEMU: `./run_iso.sh` (Linux/macOS) or `run_iso.bat` (Windows).

CI runs `make unit && build/unit` on every push (GitHub Actions, ubuntu-latest).

### Build targets

- **kernel**: compiles `kernel/` C/ASM → links with embedded user tar → `build/wheel.elf`
- **users**: compiles `user/` programs → ELFs → packed into tar → objcopy'd into kernel `.rodata`
- **unit**: compiles kernel C as `-fPIC -DUNIT_TEST` into `libwheel.so`, links with gtest `.cc` files
- **iso**: wraps `wheel.elf` with GRUB into a bootable ISO

User programs live under `user/` (e.g. `user/test3/`, `user/demo_float/`) with a shared libc under `user/libc/`. The build packs all user ELFs into a tar archive, then embeds it via `objcopy -I binary` into the kernel as `_binary_users_tar_start` / `_binary_users_tar_end`.

## Architecture Overview

**Wheel** is a 64-bit higher-half kernel for x86_64, written from scratch. Supports ring-3 user mode, multiprocessor, preemptive scheduling, framebuffer console.

### Kernel source layout (`kernel/`)

| Directory | Purpose |
|-----------|---------|
| `arch_x86_64/` | X86_64-specific code: boot, ACPI, APIC (Local + IO), CPU features, GDT/IDT/TSS, devices, MMU |
| `api/` | Architecture-independent API headers: `wheel.h` (common macros), `arch_api_common.h` (arch interface) |
| `core/` | Architecture-independent OS primitives: task/scheduler, spinlock (MCS), wdog, semaphore, mutex, msgq, kobj, ELF loader, process (proc), syscall |
| `lib/` | Data structures and utilities: doubly-linked list, rbtree, fifo, kstring/format, tar parser |
| `mem/` | Physical page allocator (buddy), virtual address space (vmspace), heap, SLUB pool, early allocator, PML layout |
| `debug/` | Debug support: ubsan, stack unwinding, kshell commands, user program test harness |
| `services/` | Kernel services: console, kshell, keyboard, PCI, block device, FAT32, partition |

### Key virtual address space layout (`arch_x86_64/arch_config.h`)

- Kernel code: `0xFFFFFFFF80000000` (-2G, higher-half)
- Direct-map of all physical memory: `0xFFFF800000000000` (16 TB virtual)
- Kernel stacks zone: `0xFFFF900000000000` – `0xFFFFA00000000000`
- Physical load address: `0x100000` (1 MB)

### Architecture / arch API boundary

`kernel/api/arch_api_common.h` defines the interface that each architecture must implement:
- **SMP & per-CPU data**: `cpu_count()`, `cpu_index()`, `percpu_ptr()`, `thiscpu_ptr()` — x86_64 uses GS segment for fast per-CPU access via macros `THISCPU(x)` / `PERCPU(i,x)`
- **Interrupts**: `cpu_int_depth()`, `cpu_int_lock()`/`cpu_int_unlock()`, `cpu_int_disable()`/`cpu_int_restore()`
- **MMU**: `mmu_create()`/`mmu_delete()`, `mmu_map()`/`mmu_unmap()`, `mmu_usetable()`, `mmu_copykernel()`, `mmu_translate()`, `tlb_shootdown()`
- **Task context switch**: `arch_task_init()`, `arch_task_switch()` (saves current task state as fake interrupt frame, restores target task via `iretq`)
- **Ring-3 entry**: `arch_enter_ring3(entry, ustack_top)` — uses `sysretq` to jump to user mode

### Linker script sections (`layout.ld`)

The kernel uses custom sections with dedicated program headers: `boot`, `percpu`, `real`, `init`, `text`, `rodata`, `data`. `.init.text`/`.init.data`/`.init.bss` are freed after boot. `.percpu.data`/`.percpu.bss` hold per-CPU variable templates that get copied per-core. User programs embedded via objcopy land in `.rodata`.

### Scheduler design (`core/task.c`)

- 32 priority levels (0=highest, 31=lowest, reserved for idle)
- Per-CPU ready queue (`prioq_t`) with bitmask for fast highest-priority lookup
- Round-robin within the same priority (tick-based time slice)
- Idle tasks run on each CPU; new tasks are placed on an idle CPU if one exists, otherwise on the current CPU
- Load balancing: `g_idle_mask` tracks idle CPUs, `g_next_cpu` for round-robin distribution when no CPU is idle
- Preemption guard: `g_preempt_depth` (per-CPU) — when > 0, `arch_task_switch` and ISR return skip task switching
- Task switch now updates CR3 when prev/next tasks belong to different address spaces

### Task lifecycle

```
task_make()        — creates STOPPED task (stack + arch state, refcnt=1)
task_start()       — TS_STOPPED → TS_READY, places in ready queue, returns target CPU mask
task_start_now()   — same + calls arch_task_switch() if targeting current CPU
task_pend()        — current task blocks: TS_READY → TS_PENDING, moved to wait queue, optional wdog for timeout
task_cont()        — clears state bits; when last bit cleared (state→READY), inserts into ready queue
task_unpend_one_nolock() — claims one waiter from a wait queue (held lock), sets got=1
task_unpend_finish()     — cancels wdog, calls task_cont(), sends resched IPI if needed (NO lock held)
task_exit()        — current task terminates: stops self, wakes joiners, defers kobj_drop via work queue
task_join_and_drop() — blocks until target reaches TS_STOPPED, then drops reference
task_drop()        — drops reference without waiting
```

**Critical protocol**: `task_unpend_one_nolock` + `task_unpend_finish` are split into lock-inside and lock-outside phases. This avoids deadlock between `wdog_cancel` (which may spin-wait for a firing callback) and the timeout callback (which acquires the same lock). Caller holds the waitq's lock during `_nolock`, releases it, then calls `_finish`.

### Kernel object framework (`core/kobj.c`)

`kclass_t` manages a class of objects; each object (`kobj_t`) has:
- Reference counting (`_Atomic int refcnt`)
- Named lookup within its class (`kobj_find`)
- Automatic destructor call when refcnt hits 0 (`kobj_drop` → dtor → `kobj_free`)
- Objects are allocated from a SLUB pool, tracked via a linked list in `kclass_t`

Common pattern:
```c
static kclass_t g_foo_class;
INIT_TEXT void foo_init() { kclass_register(&g_foo_class, "foo", sizeof(foo_t), foo_cleanup); }
foo_t *foo = kobj_make(&g_foo_class, "name");  // refcnt=1
kobj_keep(foo);     // refcnt++
kobj_drop(&g_foo_class, foo);  // refcnt--, may trigger dtor + free
```

### Synchronization primitives

**MCS spinlock** (`core/spinlock.h`, `core/spinlock.c`):
- Queue-based (Mellor-Crummey-Scott): each waiter spins on its own cache line, no contention on the lock word itself
- All `spinlock_take()` calls disable interrupts (unified API — no raw/irq split)
- `spinlock_node_t` is passed by caller (stack-allocated), stored in percpu `g_held[]` for lockdep
- RAII via `SPINLOCK_SCOPED(lock)` using `__attribute__((cleanup))` — automatically releases on scope exit
- Lockdep (DEBUG only): percpu held-lock stack detects reentrancy and out-of-order release. Requires `enable_lockdep()` after percpu is initialized.

**Watchdog timer** (`core/wdog.c`):
- Atomic state machine: `WDOG_IDLE → ARMED → FIRED → IDLE`
- `wdog_cancel`: CAS from ARMED→IDLE (inside timer_lock); if CAS fails (ISR already claimed), spin-waits for callback completion OUTSIDE the lock
- This two-phase design prevents deadlock between timeout callbacks and wdog cancellation

**Semaphore / Mutex / Message Queue** (`core/sema.c`, `core/mutex.c`, `core/msgq.c`):
- All three are built on kobj, using the split lock protocol (`task_unpend_one_nolock` + `task_unpend_finish`)
- msgq uses a kernel-mapped ring buffer (fifo), supports blocking send/recv with timeout

### Virtual memory (`mem/vmspace.c`)

- `vmspace_t` represents an address space: ordered list of `vmrange_t`, plus a page table
- `g_kernel_vm` is the kernel's shared address space (PML4 entries 256–511 shared by all processes)
- `vmspace_alloc()` reserves virtual range + allocates physical pages + creates mappings
- `vmspace_alloc_at()` maps at a specific virtual address (used by ELF loader)
- `vmrange_t.pages` (pglist_t) tracks physical pages — replaces the old `paddr` field, unifying contiguous and non-contiguous physical memory
- `mmu_create()` creates a new empty PML4; `mmu_copykernel()` copies kernel-half entries
- On task switch, CR3 is conditionally updated (`cmovne`) if prev/next tasks have different page tables

### Physical memory (`mem/page.h`, `mem/page.c`)

Buddy allocator (rank 0–15, max 2^15 pages = 128 MB blocks). `page_t` descriptors track head/rank/type/ent_num/objects.

- `page_alloc(rank, type)` — allocate a single block of given rank
- `page_alloc_color(rank, type, period, phase)` — page-coloring variant for cache optimization
- `page_free(pa)` — free a single block
- `pagelist_alloc(pl, num, type)` — allocate N pages as discontiguous blocks, stored in pglist
- `pagelist_free(pl)` — free all blocks in a pglist
- `g_free_cnt` tracks total free pages (validated via `page_free_count()`)

**SLUB pool** (`mem/pool_slub.c`): fixed-size object allocator. Each slab = 1 page, divided into objects linked by embedded freelist. Slabs sorted into empty/partial/full lists. Pool functions are `_nolock` — caller must hold the appropriate lock.

**Kernel heap** (`mem/heap.c`): best-fit allocator using red-black tree indexed by chunk size. Guards at both ends prevent underflow/overflow coalescing.

### Process model (`core/proc.c`, `core/proc.h`)

Linux-like model: tasks share a process as a resource (`task_t.process` pointer).

```c
proc_t *proc_make(name)       — create process with fresh address space
task_enter_process(pid)       — current task joins process (kobj_keep, switch cr3)
task_leave_process()          — current task leaves (switch to kernel cr3, kobj_drop)
proc_valloc(pid, addr, size)  — allocate vmrange in process's address space
```

A process's address space persists after its last task exits (refcnt keeps it alive). The cleanup function (`proc_cleanup`) is called when refcnt hits 0.

### ELF loader (`core/elf.c`)

Loads statically-linked x86_64 ELF executables (`ET_EXEC`). Maps each `PT_LOAD` segment via `proc_valloc`, copies file data, zeros BSS, then remaps with final permissions (`elf_to_mmu_attr`). Returns entry point address.

### Boot flow (`arch_x86_64/start/init.c`)

1. GRUB → `sys_init()` (multiboot1 or multiboot2)
2. Parse memory map, framebuffer, ACPI tables
3. Initialize GDT/IDT, memory management, percpu
4. Enable lockdep, init interrupt controllers (Local APIC, IO APIC)
5. Load final page tables, init scheduler and kobj classes
6. Create root task → `root_proc()` starts APs via INIT-SIPI
7. Init keyboard, ATA, kshell, then `reclaim_init()` frees init sections

### TLB shootdown (`arch_x86_64/mem/mmu.c`)

Uses CAS loop on `g_shootdown_cnt` instead of spinlock — CAS doesn't disable interrupts, allowing the waiting CPU to receive IPIs. After winning CAS, sets shared range vars, sends IPI to all other CPUs, spins until all complete.

## Unit Testing

Tests are written in C++ using Google Test (`libgtest-dev`), compiled with the host toolchain. Kernel C code is compiled separately as `-fPIC -DUNIT_TEST -DDEBUG` into `libwheel.so`, then linked with the test `.cc` files.

Key test files:
- `kernel/lib/*.test.cc` — dllist, kstring, format, rbtree
- `kernel/mem/page.test.cc` — physical page allocator
- `kernel/arch_x86_64/arch_mock.cc` / `kernel/mem/early_alloc.mock.cc` — mock implementations for unit tests

```bash
make kunit && LD_LIBRARY_PATH=build build/kunit # build and run
make kcov                                       # build + run + HTML coverage
```

## Source Conventions

- `.c` files in `kernel/` are kernel code; `.cc` files are unit tests or mocks
- Indent using 4 spaces
- `INIT_TEXT` / `INIT_DATA` / `INIT_BSS` macros place code/data in init sections that are freed after boot
- `PERCPU_DATA` / `PERCPU_BSS` macros mark per-CPU variables
- `CONST` places variables in `.rodata` (writable during early boot, read-only after final page tables loaded)
- `UNUSED`, `PACKED`, `NORETURN`, `PRINTF(s,a)` are defined in `api/wheel.h`
- Comments and identifiers are in Chinese (functions/variables: English snake_case; file names: English)
- Include guard names omit the directory: `SPIN_H` not `LIB_SPIN_H`, `TASK_H` not `CORE_TASK_H`. Headers under `kernel/arch/` use full path: `ARCH_X86_64_BGA_H`
- Host tools live in `host/` (PSF font parser, BIOS roms, disk image helper)
- Design notes and drafts live in `ramblings/` (not compiled)
- Architecture documentation lives in `docs/`

### `CONST` and `INIT_*` section macros

`CONST` places a variable in `.rodata`, which becomes read-only after the kernel's final page tables are loaded. During early boot (before `write_cr3(g_kernel_vm.table)`), all memory is writable, so `CONST` variables can still be modified by init code. Use this for "write once during boot, read-only forever after" data.

`INIT_TEXT` / `INIT_DATA` / `INIT_BSS` place code or data in `.init.*` sections that are freed (reclaimed as free pages) after boot completes. Use `INIT_TEXT` for functions only called during startup, `INIT_DATA`/`INIT_BSS` for their associated data.

Common pattern — a flag set once during init, read-only thereafter:
```c
static CONST int g_ready;
INIT_TEXT void init_complete() { g_ready = 1; }
```
