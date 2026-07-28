#include "apic.h"
#include <arch_api.h>
#include <debug.h>


// IO APIC 内存映射寄存器
#define IO_REG_SEL      0x00
#define IO_REG_WIN      0x10
#define IO_REG_EOI      0x40

// 间接寄存器，通过 sel、win 访问
#define IOAPIC_ID       0x00
#define IOAPIC_VER      0x01
#define IOAPIC_ARB      0x02
#define IOAPIC_BOOT     0x03            // boot configuration
#define IOAPIC_RED_L(i) (0x10 + 2*(i))
#define IOAPIC_RED_H(i) (0x11 + 2*(i))

// redirection table entry upper 32bit
#define IOAPIC_DST      0xff000000

// redirection table entry lower 32 bit
#define IOAPIC_INT_MASK 0x00010000
#define IOAPIC_LEVEL    0x00008000
#define IOAPIC_EDGE     0x00000000
#define IOAPIC_REMOTE   0x00004000
#define IOAPIC_LOW      0x00002000
#define IOAPIC_HIGH     0x00000000
#define IOAPIC_LOGICAL  0x00000800
#define IOAPIC_PHYSICAL 0x00000000

// delivery mode
#define IOAPIC_DM_FIXED    0x00000000
#define IOAPIC_DM_LOWEST   0x00000100
#define IOAPIC_DM_SMI      0x00000200
#define IOAPIC_DM_NMI      0x00000400
#define IOAPIC_DM_INIT     0x00000500
#define IOAPIC_DM_EXTINT   0x00000700

#define IOAPIC_VEC_MASK 0x000000ff


static uint32_t ioapic_read(const ioapic_t *io, uint32_t reg) {
    *(volatile uint32_t*)(idmap_at(io->addr) + IO_REG_SEL) = reg;
    return *(volatile uint32_t*)(idmap_at(io->addr) + IO_REG_WIN);
}

static void ioapic_write(const ioapic_t *io, uint32_t reg, uint32_t val) {
    *(volatile uint32_t*)(idmap_at(io->addr) + IO_REG_SEL) = reg;
    *(volatile uint32_t*)(idmap_at(io->addr) + IO_REG_WIN) = val;
}


// x2APIC 使用 32-bit ID，可是 IO APIC redirection entry 仍使用 8-bit dest
// 如果遇到超过 256 的 Local APIC ID，IO APIC 就无法将中断发给这个 CPU
// （我们只是 hobby OS，其实无需担心）
// 解决方案一：使用 logical dest mode
//      能处理 x2APIC-ID 超过 256 的情况，但 CPU 总数超过 256 仍然解决不了
// 解决方案二：Interrupt Remapping（主流）
//      使用 Intel VT-d 或 AMD-Vi IOMMU
//      IO APIC 发送的中断首先经过 IOMMU，由 remapping table 翻译成 32-bit x2APIC-ID
// 解决方案三：绕过 IO APIC（首选）
//      使用 MSI/MSI-X，让 PCI 设备直接写 Local APIC，直接向 CPU 发送中断
//      绕过 IO APIC，还能减少一跳，中断延迟更低
//      对于 ISA/LPC 遗留中断（必须走 IO APIC），则只能发送给小于 256 的低号 CPU

INIT_TEXT void ioapic_init() {
    uint32_t gsi = 0;
    for (int i = 0; i < g_ioapic_num; ++i) {
        ioapic_t *io = &g_ioapics[i];
        uint32_t id = (ioapic_read(io, IOAPIC_ID) >> 24) & 0x0f;
        uint32_t ver = ioapic_read(io, IOAPIC_VER);

        if (id != io->apic_id) {
            logk("IO-APIC .%d id=%u\n", id, io->apic_id);
        }

        io->ver = ver & 0xff;
        io->red_num = ((ver >> 16) & 0xff) + 1;
        io->gsi_base = gsi;
        // gsi += io->red_num;

        // 这些中断固定发送给 CPU0
        // TODO 改造成广播 lowest priority 模式，发送给优先级最低的 CPU，负载均衡

        // 前面 16 个 GSI 继承自 8259A，也就是 IRQ
        int ent = 0;
        for (; (ent < io->red_num) && (gsi < 16); ++ent, ++gsi) {
            // uint32_t lo = IOAPIC_DM_LOWEST | IOAPIC_LOGICAL;
            uint32_t lo = IOAPIC_DM_FIXED;
            lo |= gsi_is_edge(gsi) ? IOAPIC_EDGE : IOAPIC_LEVEL;
            lo |= gsi_is_high(gsi) ? IOAPIC_HIGH : IOAPIC_LOW;
            lo |= (gsi + VEC_GSI_BASE) & IOAPIC_VEC_MASK;
            lo |= IOAPIC_INT_MASK;
            // ioapic_write(io, IOAPIC_RED_H(ent), 0xff000000); // 广播
            ioapic_write(io, IOAPIC_RED_H(gsi), g_loapics[0].apic_id << 24);
            ioapic_write(io, IOAPIC_RED_L(ent), lo);
        }

        // IRQ 之后的硬件中断，level-triggered，active low
        for (; ent < io->red_num; ++ent, ++gsi) {
            // uint32_t lo = IOAPIC_DM_LOWEST | IOAPIC_LOGICAL | IOAPIC_LEVEL | IOAPIC_LOW;
            uint32_t lo = IOAPIC_DM_FIXED | IOAPIC_LEVEL | IOAPIC_LOW;
            lo |= (gsi + VEC_GSI_BASE) & IOAPIC_VEC_MASK;
            lo |= IOAPIC_INT_MASK;
            // ioapic_write(io, IOAPIC_RED_H(ent), 0xff000000); // 广播
            ioapic_write(io, IOAPIC_RED_H(gsi), g_loapics[0].apic_id << 24);
            ioapic_write(io, IOAPIC_RED_L(ent), lo);
        }
    }
}

// 寻找 GSI 所对应的 IO APIC
static ioapic_t *ioapic_for_gsi(uint32_t gsi) {
    for (int i = 0; i < g_ioapic_num; ++i) {
        ioapic_t *io = &g_ioapics[i];
        if ((io->gsi_base <= gsi) && (gsi < io->gsi_base + io->red_num)) {
            return io;
        }
    }
    return NULL;
}

void ioapic_mask_gsi(uint32_t gsi) {
    ioapic_t *io = ioapic_for_gsi(gsi);
    if (NULL == io) {
        return;
    }

    gsi -= io->gsi_base;
    uint32_t red_lo = ioapic_read(io, IOAPIC_RED_L(gsi));
    red_lo |= IOAPIC_INT_MASK;
    ioapic_write(io, IOAPIC_RED_L(gsi), red_lo);
}

void ioapic_unmask_gsi(uint32_t gsi) {
    ioapic_t *io = ioapic_for_gsi(gsi);
    if (NULL == io) {
        return;
    }

    gsi -= io->gsi_base;
    uint32_t red_lo = ioapic_read(io, IOAPIC_RED_L(gsi));
    red_lo &= ~IOAPIC_INT_MASK;
    ioapic_write(io, IOAPIC_RED_L(gsi), red_lo);
}

// 设置硬件中断的路由，设置目标 CPU、向量号
// TODO 使用 cpuset_t 指定目标 CPU，可以把中断发给多个 CPU
void ioapic_route_gsi(uint32_t gsi, int cpu, uint8_t vec) {
    ASSERT(cpu < cpu_count());

    ioapic_t *io = ioapic_for_gsi(gsi);
    if (NULL == io) {
        return;
    }
    gsi -= io->gsi_base;

    // TODO x2APIC-ID 可能超过 8-bit，应该使用 interrupt-remapper 重新映射到 8-bit 之内
    ioapic_write(io, IOAPIC_RED_H(gsi), g_loapics[cpu].apic_id << 24);

    uint32_t lo = ioapic_read(io, IOAPIC_RED_L(gsi));
    lo &= ~IOAPIC_VEC_MASK;
    lo |= vec & IOAPIC_VEC_MASK;
    ioapic_write(io, IOAPIC_RED_L(gsi), lo);
}

void ioapic_send_eoi(int vec) {
    ASSERT(vec >= VEC_GSI_BASE);

    ioapic_t *io = ioapic_for_gsi(vec - VEC_GSI_BASE);
    if (io && (io->ver >= 0x20)) {
        *(volatile uint32_t*)(idmap_at(io->addr) + IO_REG_EOI) = vec;
    }
}
