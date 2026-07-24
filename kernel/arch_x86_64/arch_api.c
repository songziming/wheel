#include "arch_api.h"
#include "mem/mem.h"
#include <acpi/mcfg.h>
#include "arch_int.h"
#include <debug.h>

#include <pci.h>

#include <task.h>
#include <kstring.h>
#include <arch_config.h>


// arch-api 涉及架构的方方面面，实现代码也分散在不同文件里
// 这里不是 arch-api 的全部，而是无法分类的部分


//------------------------------------------------------------------------------
// stack unwind
//------------------------------------------------------------------------------

// AMD64 栈结构（向下生长）：
// high |     arg 8     |
//      |     arg 7     |
//      |  return addr  | <- 刚跳转之后的 RSP
//      |    old RBP    | <- RBP
//      |  local var x  |
//  low |  local var y  | <- RSP
int arch_unwind_from(size_t *addrs, int max, uint64_t rbp) {
    int i = 0;

    for (; (i < max) && (0 != rbp); ++i) {
        uint64_t *frame = (uint64_t*)rbp;
        addrs[i] = (size_t)frame[1];
        if (0 == addrs[i]) {
            break;
        }
        rbp = frame[0];
    }

    return i;
}

// 获取当前调用栈，返回深度
int arch_unwind(size_t *addrs, int max) {
    uint64_t rbp;
    ASMV("movq %%rbp, %0" : "=r"(rbp));
    return arch_unwind_from(addrs, max, rbp);
}

//------------------------------------------------------------------------------
// PCI(e) support
//------------------------------------------------------------------------------

#define CONFIG_ADDR 0xcf8
#define CONFIG_DATA 0xcfc

static uint32_t pci_read_io(uint8_t bus, uint8_t dev, uint8_t func, uint8_t reg) {
    ASSERT(dev < 32);
    ASSERT(func < 8);
    ASSERT(0 == (reg & 3));

    uint32_t addr = ((uint32_t)bus  << 16)
                  | ((uint32_t)dev  << 11)
                  | ((uint32_t)func <<  8)
                  |  (uint32_t)reg
                  | 0x80000000U;
    out32(CONFIG_ADDR, addr);
    return in32(CONFIG_DATA);
}

static void pci_write_io(uint8_t bus, uint8_t dev, uint8_t func, uint8_t reg, uint32_t data) {
    ASSERT(dev < 32);
    ASSERT(func < 8);
    ASSERT(0 == (reg & 3));

    uint32_t addr = ((uint32_t)bus  << 16)
                  | ((uint32_t)dev  << 11)
                  | ((uint32_t)func <<  8)
                  |  (uint32_t)reg
                  | 0x80000000U;
    out32(CONFIG_ADDR, addr);
    out32(CONFIG_DATA, data);
}

// ECAM 虚拟地址基址（通过 direct-map 映射）
static CONST char *g_ecam_base;

// ECAM 地址计算：每个 bus 1MB，每个 device 32KB，每个 function 4KB
static inline void *ecam_addr(uint8_t bus, uint8_t dev, uint8_t func, uint16_t reg) {
    size_t off = ((size_t)bus  << 20)
               | ((size_t)dev  << 15)
               | ((size_t)func << 12)
               |  (size_t)reg;
    return g_ecam_base + off;
}

static uint32_t pci_read_mmio(uint8_t bus, uint8_t dev, uint8_t func, uint8_t reg) {
    ASSERT(dev < 32);
    ASSERT(func < 8);
    ASSERT(0 == (reg & 3));
    return *(volatile uint32_t*)ecam_addr(bus, dev, func, reg);
}

static void pci_write_mmio(uint8_t bus, uint8_t dev, uint8_t func, uint8_t reg, uint32_t data) {
    ASSERT(dev < 32);
    ASSERT(func < 8);
    ASSERT(0 == (reg & 3));
    *(volatile uint32_t*)ecam_addr(bus, dev, func, reg) = data;
}

// ---- 初始化 ------------------------------------------------------------------

INIT_TEXT void arch_pci_init() {
    mcfg_t *mcfg = (mcfg_t*)acpi_table_find("MCFG", 0);
    if (mcfg) {
        ecam_t *entries = mcfg->entries;
        int num = (mcfg->header.length - 44) / (int)sizeof(ecam_t);

        // 查找 segment-group 0（大多数系统只有一个 segment）
        for (int i = 0; i < num; i++) {
            if (0 == entries[i].segment_group) {
                // 通过 direct-map 将 ECAM 物理地址映射到虚拟地址
                g_ecam_base = idmap_at(entries[i].base_address);
                g_pci_read  = pci_read_mmio;
                g_pci_write = pci_write_mmio;
                logk("PCIe ECAM base=0x%lx, bus %d-%d\n",
                    entries[i].base_address,
                    entries[i].start_bus, entries[i].end_bus);
                return;
            }
        }

        // 没找到 segment 0，回退到传统方式
        logk("MCFG found but no segment 0, falling back to PCI I/O\n");
    }

    // 传统 PCI 方式
    g_pci_read  = pci_read_io;
    g_pci_write = pci_write_io;
    logk("using legacy PCI I/O (0xcf8/0xcfc)\n");
}

//------------------------------------------------------------------------------
// task support
//------------------------------------------------------------------------------

// 填写栈顶的寄存器
void arch_task_init(task_t *task, size_t entry, size_t stack_top,
        size_t arg1, size_t arg2, size_t arg3, size_t arg4) {
    stack_top &= ~15UL;  // 栈顶按 16 字节对齐

    // 写入 dummy-return-addr，防止任务中 backtrace 越界
    stack_top -= 16;
    *(uint64_t*)stack_top = (uint64_t)task_exit;

    regs_t *regs = (regs_t*)(stack_top - sizeof(regs_t));
    kmemset(regs, 0, sizeof(regs_t));

    regs->cs = 0x08;
    regs->ss = 0x10;
    regs->rflags = 0x0200UL;    // 开启中断
    regs->rsp = stack_top;
    regs->rip = entry;

    regs->rdi = arg1;
    regs->rsi = arg2;
    regs->rdx = arg3;
    regs->rcx = arg4;

    task->stack_top = (size_t)regs;
}

// 默认的 FPU 初始状态，启动时由 fpu_init() 捕获
// 新任务创建时从这里 copy 初始状态
CONST arch_fp_t g_fp_init_state;

INIT_TEXT void fpu_init() {
    ASMV("fninit");

    // MXCSR default: all exceptions masked, round-to-nearest
    uint32_t mxcsr = 0x1f80;
    ASMV("ldmxcsr %0" :: "m"(mxcsr));
    ASMV("fxsave (%0)" :: "r"(&g_fp_init_state));
}
