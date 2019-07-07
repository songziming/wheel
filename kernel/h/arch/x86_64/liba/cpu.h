#ifndef ARCH_X86_64_LIBA_CPU_H
#define ARCH_X86_64_LIBA_CPU_H

#include <base.h>
#include <arch.h>

//------------------------------------------------------------------------------
// address mapping

static inline usize virt_to_phys(void * va) {
    if ((usize) va >= KERNEL_VMA) {
        return (usize) va - KERNEL_VMA + KERNEL_LMA;
    } else if (((u8 *) MAPPED_ADDR <= (u8 *) va) &&
               ((u8 *) va < (u8 *) MAPPED_ADDR + MAPPED_SIZE)) {
        return (usize) va - MAPPED_ADDR;
    }
    return NO_ADDR;
}

static inline void * phys_to_virt(usize pa) {
    if (pa < MAPPED_SIZE) {
        return (void *) (pa + MAPPED_ADDR);
    }
    return NULL;
}

//------------------------------------------------------------------------------
// inline assembly routines

#define DEFINE_READ(n)                                  \
static inline u ## n read ## n (void * p) {             \
    return * ((u ## n volatile *) p);                   \
}
#define DEFINE_WRITE(n)                                 \
static inline void write ## n (void * p, u ## n x)  {   \
    * ((u ## n volatile *) p) = x;                      \
}

DEFINE_READ (8)         // read8
DEFINE_READ (16)        // read16
DEFINE_READ (32)        // read32
DEFINE_READ (64)        // read64
DEFINE_WRITE(8)         // write8
DEFINE_WRITE(16)        // write16
DEFINE_WRITE(32)        // write32
DEFINE_WRITE(64)        // write64

#define DEFINE_IN(n, suffix)                            \
static inline u ## n in ## n (u16 p) {                  \
    u ## n x;                                           \
    ASM("in" suffix " %1, %0" : "=a"(x) : "Nd"(p));     \
    return x;                                           \
}
#define DEFINE_OUT(n, suffix)                           \
static inline void out ## n (u16 p, u ## n x) {         \
    ASM("out" suffix " %0, %1" :: "a"(x), "Nd"(p));     \
}

DEFINE_IN (8,  "b")     // in8
DEFINE_IN (16, "w")     // in16
DEFINE_IN (32, "l")     // in32
DEFINE_OUT(8,  "b")     // out8
DEFINE_OUT(16, "w")     // out16
DEFINE_OUT(32, "l")     // out32

#define DEFINE_READ_CR(n)                               \
static inline u64 read_cr ## n () {                     \
    u64 x;                                              \
    ASM("mov %%cr" #n ", %0" : "=r"(x));                \
    return x;                                           \
}
#define DEFINE_WRITE_CR(n)                              \
static inline void write_cr ## n (u64 x) {              \
    ASM("mov %0, %%cr" #n :: "r"(x));                   \
}

DEFINE_READ_CR (0)      // read_cr0
DEFINE_READ_CR (2)      // read_cr2
DEFINE_READ_CR (3)      // read_cr3
DEFINE_READ_CR (4)      // read_cr4
DEFINE_WRITE_CR(0)      // write_cr0
DEFINE_WRITE_CR(2)      // write_cr2
DEFINE_WRITE_CR(3)      // write_cr3
DEFINE_WRITE_CR(4)      // write_cr4

static inline void io_wait() {
    ASM("outb %%al, $0x80" :: "a"(0));
}

static inline void cpu_sleep() {
    ASM("hlt");
}

static inline void cpu_relax() {
    ASM("pause");
}

static inline void cpu_fence() {
    ASM("mfence" ::: "memory");
}

static inline void cpuid(u32 * a, u32 * b, u32 * c, u32 * d) {
    u32 eax, ebx, ecx, edx;
    if (NULL == a) {
        eax = 0;
        a   = &eax;
    }
    if (NULL == b) {
        ebx = 0;
        b   = &ebx;
    }
    if (NULL == c) {
        ecx = 0;
        c   = &ecx;
    }
    if (NULL == d) {
        edx = 0;
        d   = &edx;
    }
    ASM("cpuid" : "=a"(*a), "=b"(*b), "=c"(*c), "=d"(*d)
                :  "a"(*a),  "b"(*b),  "c"(*c),  "d"(*d));
}

// read msr registers
static inline u64 read_msr(u32 msr) {
    union {
        u32 d[2];
        u64 q;
    } u;
    ASM("rdmsr" : "=d"(u.d[1]), "=a"(u.d[0]) : "c"(msr));
    return u.q;
}

// write msr registers
static inline void write_msr(u32 msr, u64 val) {
    union {
        u32 d[2];
        u64 q;
    } u;
    u.q = val;
    ASM("wrmsr" :: "d"(u.d[1]), "a"(u.d[0]), "c"(msr));
}

// special msr registers
static inline u64 read_fsbase() {
    return read_msr(0xc0000100U);
}

static inline u64 read_gsbase() {
    return read_msr(0xc0000101U);
}

static inline u64 read_kgsbase() {
    return read_msr(0xc0000102U);
}

static inline void write_fsbase(u64 val) {
    write_msr(0xc0000100U, val);
}

static inline void write_gsbase(u64 val) {
    write_msr(0xc0000101U, val);
}

static inline void write_kgsbase(u64 val) {
    write_msr(0xc0000102U, val);
}

//------------------------------------------------------------------------------
// kernel debug support

extern void dbg_trace_here();
extern void dbg_trace_from(u64 rip, u64 * rbp);
extern void dbg_write_text(const char * s, usize len);

extern __INIT void regist_symtbl(void * tbl, usize len);
extern __INIT void regist_strtbl(void * tbl, usize len);

//------------------------------------------------------------------------------
// multi-processor support

extern            int cpu_installed;
extern __INITDATA int cpu_activated;
extern            u64 percpu_base;
extern            u64 percpu_size;

extern int    cpu_count();
extern int    cpu_index();
extern void * calc_percpu_addr(u32 cpu, void * ptr);
extern void * calc_thiscpu_addr(void * ptr);

#define percpu_ptr(i, var)  ((TYPE(&var)) calc_percpu_addr(i, &var))
#define thiscpu_ptr(var)    ((TYPE(&var)) calc_thiscpu_addr(&var))
#define percpu_var(i, var)  (* percpu_ptr(i, var))
#define thiscpu_var(var)    (* thiscpu_ptr(var))

//------------------------------------------------------------------------------
// essential cpu features

extern __INIT void cpu_init();
extern __INIT void gdt_init();  // requires: allot
extern __INIT void idt_init();
extern __INIT void tss_init();  // requires: percpu-var

//------------------------------------------------------------------------------
// exception and interrupt

extern __PERCPU int    int_depth;
extern __PERCPU int    int_rsp;
extern          void * isr_tbl[256];

extern void int_disable();
extern void int_enable ();
extern u32  int_lock   ();
extern void int_unlock (u32 key);

extern __INIT void int_init();  // requires: percpu-var

#endif // ARCH_X86_64_LIBA_CPU_H
