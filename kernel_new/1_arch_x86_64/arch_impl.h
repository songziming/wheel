#ifndef ARCH_IMPL_H
#define ARCH_IMPL_H

#define PAGE_SHIFT  12
#define PAGE_SIZE   0x1000


#ifdef C_FILE

typedef struct regs {
    uint64_t rax;
} regs_t;

#endif // C_FILE

#endif // ARCH_IMPL_H
