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

extern usize dbg_sym_locate(const char * name);
extern void  dbg_trace_here();
extern void  dbg_trace_from(u64 rip, u64 * rbp);
extern void  dbg_write_text(const char * s, usize len);

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

enum {
    CPUID_FEAT_ECX_SSE3     = 1 << 0,
    CPUID_FEAT_ECX_PCLMUL   = 1 << 1,
    CPUID_FEAT_ECX_DTES64   = 1 << 2,
    CPUID_FEAT_ECX_MONITOR  = 1 << 3,
    CPUID_FEAT_ECX_DS_CPL   = 1 << 4,
    CPUID_FEAT_ECX_VMX      = 1 << 5,
    CPUID_FEAT_ECX_SMX      = 1 << 6,
    CPUID_FEAT_ECX_EST      = 1 << 7,
    CPUID_FEAT_ECX_TM2      = 1 << 8,
    CPUID_FEAT_ECX_SSSE3    = 1 << 9,
    CPUID_FEAT_ECX_CID      = 1 << 10,
    CPUID_FEAT_ECX_FMA      = 1 << 12,
    CPUID_FEAT_ECX_CX16     = 1 << 13,
    CPUID_FEAT_ECX_ETPRD    = 1 << 14,
    CPUID_FEAT_ECX_PDCM     = 1 << 15,
    CPUID_FEAT_ECX_PCIDE    = 1 << 17,
    CPUID_FEAT_ECX_DCA      = 1 << 18,
    CPUID_FEAT_ECX_SSE4_1   = 1 << 19,
    CPUID_FEAT_ECX_SSE4_2   = 1 << 20,
    CPUID_FEAT_ECX_x2APIC   = 1 << 21,
    CPUID_FEAT_ECX_MOVBE    = 1 << 22,
    CPUID_FEAT_ECX_POPCNT   = 1 << 23,
    CPUID_FEAT_ECX_AES      = 1 << 25,
    CPUID_FEAT_ECX_XSAVE    = 1 << 26,
    CPUID_FEAT_ECX_OSXSAVE  = 1 << 27,
    CPUID_FEAT_ECX_AVX      = 1 << 28,
 
    CPUID_FEAT_EDX_FPU      = 1 << 0,
    CPUID_FEAT_EDX_VME      = 1 << 1,
    CPUID_FEAT_EDX_DE       = 1 << 2,
    CPUID_FEAT_EDX_PSE      = 1 << 3,
    CPUID_FEAT_EDX_TSC      = 1 << 4,
    CPUID_FEAT_EDX_MSR      = 1 << 5,
    CPUID_FEAT_EDX_PAE      = 1 << 6,
    CPUID_FEAT_EDX_MCE      = 1 << 7,
    CPUID_FEAT_EDX_CX8      = 1 << 8,
    CPUID_FEAT_EDX_APIC     = 1 << 9,
    CPUID_FEAT_EDX_SEP      = 1 << 11,
    CPUID_FEAT_EDX_MTRR     = 1 << 12,
    CPUID_FEAT_EDX_PGE      = 1 << 13,
    CPUID_FEAT_EDX_MCA      = 1 << 14,
    CPUID_FEAT_EDX_CMOV     = 1 << 15,
    CPUID_FEAT_EDX_PAT      = 1 << 16,
    CPUID_FEAT_EDX_PSE36    = 1 << 17,
    CPUID_FEAT_EDX_PSN      = 1 << 18,
    CPUID_FEAT_EDX_CLF      = 1 << 19,
    CPUID_FEAT_EDX_DTES     = 1 << 21,
    CPUID_FEAT_EDX_ACPI     = 1 << 22,
    CPUID_FEAT_EDX_MMX      = 1 << 23,
    CPUID_FEAT_EDX_FXSR     = 1 << 24,
    CPUID_FEAT_EDX_SSE      = 1 << 25,
    CPUID_FEAT_EDX_SSE2     = 1 << 26,
    CPUID_FEAT_EDX_SS       = 1 << 27,
    CPUID_FEAT_EDX_HTT      = 1 << 28,
    CPUID_FEAT_EDX_TM1      = 1 << 29,
    CPUID_FEAT_EDX_IA64     = 1 << 30,
    CPUID_FEAT_EDX_PBE      = 1 << 31
};

extern char vendor_id[13];
extern u32  feat_ecx;
extern u32  feat_edx;

extern __INIT void cpu_init();
extern __INIT void gdt_init();  // requires: allot
extern __INIT void idt_init();
extern __INIT void tss_init();  // requires: percpu-var, thiscpu-var

//------------------------------------------------------------------------------
// exception and interrupt

// register frame saved on stack during interrupt and exception
typedef struct int_frame {
    u64 r15;    u64 r14;    u64 r13;    u64 r12;
    u64 r11;    u64 r10;    u64 r9;     u64 r8;
    u64 rbp;    u64 rsi;    u64 rdi;    u64 rdx;
    u64 rcx;    u64 rbx;    u64 rax;    u64 errcode;
    u64 rip;    u64 cs;     u64 rflags; u64 rsp;    u64 ss;
} __PACKED int_frame_t;

extern __PERCPU int    int_depth;
extern __PERCPU usize  int_rsp;
extern          void * isr_tbl[256];

extern void int_disable();
extern void int_enable ();
extern u32  int_lock   ();
extern void int_unlock (u32 key);

extern __INIT void int_init();  // requires: percpu-var

//------------------------------------------------------------------------------
// task support

// rsp and rsp0 are not redundant, since interrupts are reentrant
typedef struct regs {
    int_frame_t * rsp;      // current int stack frame
    u64           rsp0;     // value saved in tss->rsp0
    u64           cr3;      // current page table
} __PACKED __ALIGNED(16) regs_t;

extern void regs_init  (regs_t * regs, usize sp, void * proc,
                        void * a1, void * a2, void * a3, void * a4);
extern void task_switch();
extern void user_return(usize ip, usize sp);
extern void smp_resched(int cpu);

#endif // ARCH_X86_64_LIBA_CPU_H
