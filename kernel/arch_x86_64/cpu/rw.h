#ifndef ARCH_X86_64_CPU_RW_H
#define ARCH_X86_64_CPU_RW_H

#include <wheel.h>


//------------------------------------------------------------------------------
// port IO
//------------------------------------------------------------------------------

static inline uint8_t in8(uint16_t port) {
    uint8_t data;
    ASMV("inb %1, %0" : "=a"(data) : "Nd"(port));
    return data;
}

static inline void out8(uint16_t port, uint8_t data) {
    ASMV("outb %0, %1" :: "a"(data), "Nd"(port));
}

static inline uint16_t in16(uint16_t port) {
    uint16_t data;
    ASMV("inw %1, %0" : "=a"(data) : "Nd"(port));
    return data;
}

static inline void out16(uint16_t port, uint16_t data) {
    ASMV("outw %0, %1" :: "a"(data), "Nd"(port));
}

static inline uint32_t in32(uint16_t port) {
    uint32_t data;
    ASMV("inl %1, %0" : "=a"(data) : "Nd"(port));
    return data;
}

static inline void out32(uint16_t port, uint32_t data) {
    ASMV("outl %0, %1" :: "a"(data), "Nd"(port));
}

static inline void io_wait() {
    ASMV("outb %%al, $0x80" :: "a"(0));
}


//------------------------------------------------------------------------------
// control registers
//------------------------------------------------------------------------------

#define DEFINE_READ_CR(n)                       \
static inline uint64_t read_cr ## n () {        \
    uint64_t x;                                 \
    ASMV("movq %%cr" #n ", %0" : "=r"(x));      \
    return x;                                   \
}
#define DEFINE_WRITE_CR(n)                      \
static inline void write_cr ## n (uint64_t x) { \
    ASMV("movq %0, %%cr" #n :: "r"(x));         \
}

DEFINE_READ_CR(0)  // read_cr0
DEFINE_READ_CR(2)  // read_cr2
DEFINE_READ_CR(3)  // read_cr3
DEFINE_READ_CR(4)  // read_cr4
DEFINE_WRITE_CR(0) // write_cr0
DEFINE_WRITE_CR(2) // write_cr2
DEFINE_WRITE_CR(3) // write_cr3
DEFINE_WRITE_CR(4) // write_cr4

#undef DEFINE_READ_CR
#undef DEFINE_WRITE_CR


//------------------------------------------------------------------------------
// MSR IO
//------------------------------------------------------------------------------

enum {
    MSR_PAT     = 0x000000277,
    MSR_MISC    = 0x000001a0U,

    MSR_EFER    = 0xc0000080U,
    MSR_STAR    = 0xc0000081U,
    MSR_LSTAR   = 0xc0000082U,
    MSR_CSTAR   = 0xc0000083U,
    MSR_SFMASK  = 0xc0000084U,

    MSR_FSBASE  = 0xc0000100U,
    MSR_GSBASE  = 0xc0000101U,
    MSR_KGSBASE = 0xc0000102U,
};

static inline uint64_t read_msr(uint32_t msr) {
    uint32_t hi;
    uint32_t lo;
    ASMV("rdmsr" : "=d"(hi), "=a"(lo) : "c"(msr));
    return ((uint64_t)hi << 32) | lo;
}

static inline void write_msr(uint32_t msr, uint64_t val) {
    uint32_t hi = val >> 32;
    uint32_t lo = val & 0xffffffff;
    ASMV("wrmsr" :: "d"(hi), "a"(lo), "c"(msr));
}

static inline uint64_t read_fsbase() { return read_msr(MSR_FSBASE); }
static inline uint64_t read_gsbase() { return read_msr(MSR_GSBASE); }
static inline uint64_t read_kgsbase() { return read_msr(MSR_KGSBASE); }

static inline void write_fsbase(uint64_t val) { write_msr(MSR_FSBASE, val); }
static inline void write_gsbase(uint64_t val) { write_msr(MSR_GSBASE, val); }
static inline void write_kgsbase(uint64_t val) { write_msr(MSR_KGSBASE, val); }

static inline uint64_t read_tsc() {
    uint32_t hi;
    uint32_t lo;
    ASMV("rdtsc" : "=d"(hi), "=a"(lo));
    return ((uint64_t)hi << 32) | lo;
}

#endif // ARCH_X86_64_CPU_RW_H
