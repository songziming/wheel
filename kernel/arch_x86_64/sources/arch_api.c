#include <arch_api_p.h>
#include <arch_mem.h>
#include <arch_int.h>
#include <cpu/local_apic.h>
#include <cpu/rw.h>
#include <wheel.h>



//------------------------------------------------------------------------------
// 部分 ARCH-API 支持函数的实现
//------------------------------------------------------------------------------

inline void cpu_halt() { __asm__("hlt"); }
inline void cpu_pause() { __asm__("pause"); }
inline void cpu_rfence() { __asm__("lfence" ::: "memory"); }
inline void cpu_wfence() { __asm__("sfence" ::: "memory"); }
inline void cpu_rwfence() { __asm__("mfence" ::: "memory"); }

inline int cpu_int_lock() {
    uint64_t key;
    __asm__("pushfq; cli; popq %0" : "=r"(key));
    return (key & 0x200) ? 1 : 0;
}

inline void cpu_int_unlock(int key) {
    if (key) {
        __asm__("sti");
    }
}

// 退出 QEMU/Bochs 模拟器并返回值
NORETURN void emu_exit(int ret) {
#ifdef DEBUG
    __asm__("outl %0, %1" :: "a"(ret), "Nd"(0xf4));
#else
    (void)ret;
#endif
    while (1) {
        cpu_halt();
    }
}

// bochs magic breakpoint
void emu_break() {
    __asm__("xchgw %bx, %bx");
}


//------------------------------------------------------------------------------
// 堆栈展开
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
        uint64_t *frame = (uint64_t *)rbp;
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
    __asm__("movq %%rbp, %0" : "=r"(rbp));
    return arch_unwind_from(addrs, max, rbp);
}



//------------------------------------------------------------------------------
// PCI 读写配置空间
//------------------------------------------------------------------------------

#define CONFIG_ADDR 0xcf8
#define CONFIG_DATA 0xcfc

static uint32_t pci_read(uint8_t bus, uint8_t dev, uint8_t func, uint8_t reg) {
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

static void pci_write(uint8_t bus, uint8_t dev, uint8_t func, uint8_t reg, uint32_t data) {
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

// TODO PCIe 使用 MMIO 读写配置空间，而不是端口
//      如果 ACPI 提供了 MCFG 表，说明支持 PCIe
// TODO 实现一套 pcie 读写函数，像 xAPIC 和 x2APIC 通过函数指针动态选择


INIT_TEXT void arch_pci_lib_init(acpi_tbl_t *mcfg) {
    if (NULL == mcfg) {
        pci_lib_init(pci_read, pci_write);
    } else {
        klog("MCFG at %p, supports PCI-E\n", mcfg);
        pci_lib_init(pci_read, pci_write);
    }
}


//------------------------------------------------------------------------------
// 多任务支持
//------------------------------------------------------------------------------

// arch_entries.S
void task_entry();

void arch_tcb_init(arch_tcb_t *tcb, size_t entry, size_t stacktop, size_t args[4]) {
    ASSERT(NULL != tcb);
    ASSERT(0 != entry);
    ASSERT(0 != stacktop);

    stacktop &= ~7UL;   // 栈顶需要按 8 字节对齐

    tcb->rsp0 = stacktop;
    tcb->regs = (arch_regs_t *)(stacktop - sizeof(arch_regs_t));
    memset(tcb->regs, 0, sizeof(arch_regs_t));

    tcb->regs->cs     = 0x08UL;             // 内核数据段
    tcb->regs->ss     = 0x10UL;             // 内核代码段
    tcb->regs->rflags = 0x0200UL;           // 开启中断
    tcb->regs->rip    = (uint64_t)task_entry;
    tcb->regs->rsp    = (uint64_t)stacktop;
    tcb->regs->rax    = (uint64_t)entry;
    tcb->regs->rdi    = (uint64_t)args[0];
    tcb->regs->rsi    = (uint64_t)args[1];
    tcb->regs->rdx    = (uint64_t)args[2];
    tcb->regs->rcx    = (uint64_t)args[3];
}


// 向其他 CPU 发送 IPI，通知其执行调度操作

static void handle_resched(int vec UNUSED, arch_regs_t *f UNUSED) {
    local_apic_send_eoi();
    // 中断返回过程自动切换任务，无需任何处理
}

static void handle_stopall(int vec UNUSED, arch_regs_t *f UNUSED) {
    // local_apic_send_eoi();

    __asm__("cli");
    while (1) {
        cpu_halt();
    }
}

void arch_send_resched(int cpu) {
    ASSERT(cpu >= 0);
    ASSERT(cpu < cpu_count());
    ASSERT(cpu_index() != cpu);
    local_apic_send_ipi(cpu, VEC_IPI_RESCHED);
}

void arch_send_stopall() {
    local_apic_send_ipi(-1, VEC_IPI_STOPALL);
}

INIT_TEXT void install_resched_handlers() {
    set_int_handler(VEC_IPI_RESCHED, handle_resched);
    set_int_handler(VEC_IPI_STOPALL, handle_stopall);
}
