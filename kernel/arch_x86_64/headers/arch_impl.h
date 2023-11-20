#ifndef ARCH_IMPL_H
#define ARCH_IMPL_H

#include <def.h>


//------------------------------------------------------------------------------
// 端口 IO
//------------------------------------------------------------------------------

static inline uint8_t in_byte(uint16_t port) {
    uint8_t data;
    __asm__("inb %1, %0" : "=a"(data) : "Nd"(port));
    return data;
}

static inline void out_byte(uint16_t port, uint8_t data) {
    __asm__("outb %0, %1" :: "a"(data), "Nd"(port));
}

static inline void io_wait() {
    __asm__("outb %%al, $0x80" :: "a"(0));
}



//------------------------------------------------------------------------------
// 读写控制寄存器
//------------------------------------------------------------------------------

#define DEFINE_READ_CR(n)                       \
static inline uint64_t read_cr ## n () {        \
    uint64_t x;                                 \
    __asm__("movq %%cr" #n ", %0" : "=r"(x));   \
    return x;                                   \
}
DEFINE_READ_CR(0) // read_cr0
DEFINE_READ_CR(2) // read_cr2
DEFINE_READ_CR(3) // read_cr3
DEFINE_READ_CR(4) // read_cr4

#define DEFINE_WRITE_CR(n)                      \
static inline void write_cr ## n (uint64_t x) { \
    __asm__("movq %0, %%cr" #n :: "r"(x));     \
}
DEFINE_WRITE_CR(0) // write_cr0
DEFINE_WRITE_CR(2) // write_cr2
DEFINE_WRITE_CR(3) // write_cr3
DEFINE_WRITE_CR(4) // write_cr4



//------------------------------------------------------------------------------
// 读写 MSR 寄存器
//------------------------------------------------------------------------------

enum {
    MSR_EFER    = 0xc0000080U,
    MSR_FSBASE  = 0xc0000100U,
    MSR_GSBASE  = 0xc0000101U,
    MSR_KGSBASE = 0xc0000102U,
    MSR_MISC    = 0x000001a0U,
};

static inline uint64_t read_msr(uint32_t msr) {
    uint32_t hi;
    uint32_t lo;
    __asm__("rdmsr" : "=d"(hi), "=a"(lo) : "c"(msr));
    return ((uint64_t)hi << 32) | lo;
}

static inline void write_msr(uint32_t msr, uint64_t val) {
    uint32_t hi = val >> 32;
    uint32_t lo = val & 0xffffffff;
    __asm__("wrmsr" :: "d"(hi), "a"(lo), "c"(msr));
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
    __asm__("rdtsc" : "=d"(hi), "=a"(lo));
    return ((uint64_t)hi << 32) | lo;
}



//------------------------------------------------------------------------------
// 调试支持
//------------------------------------------------------------------------------

int arch_unwind(size_t *addrs, int max, uint64_t rbp);

#endif // ARCH_IMPL_H
