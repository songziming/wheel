#ifndef ARCH_X86_64_ARCH_H
#define ARCH_X86_64_ARCH_H

// MMU configuration
#define PAGE_SIZE       4096                    // we use 4K pages
#define PAGE_SHIFT      12                      // 4K = 2^12

// kernel image address
#define KERNEL_LMA      0x0000000001000000UL    // physical
#define KERNEL_VMA      0xffffffff81000000UL    // virtual
#define MAPPED_ADDR     0xffff800000000000UL    // higher-half
#define MAPPED_SIZE     0x0000100000000000UL    // 4K*2^32 = 16TB

// user virtual memory (vmspace)
#define USER_START      0x0000000000000000UL    // 0
#define USER_END        0x0000800000000000UL    // just below canonical hole

// memory zone layout (physical memory manager)
#define DMA_START       0x0000000000000000UL    // 0
#define DMA_END         0x0000000001000000UL    // 16MB
#define NORMAL_START    0x0000000001000000UL    // 16MB
#define NORMAL_END      0xffffffffffffffffUL    // +inf

// #include "multiboot.h"

// #include "liba/cpu.h"
// #include "liba/acpi.h"
// #include "liba/atomic.h"

// #include "liba/loapic.h"
// #include "liba/ioapic.h"

// #include "drvs/serial.h"
// #include "drvs/console.h"

#endif // ARCH_X86_64_ARCH_H
