#include <cpu/rw.h>
#include <arch_smp.h>

#include <wheel.h>


// 理论上，IO APIC 不属于 CPU，而是集成在芯片组中
// 大部分资源只给出了 82093AA 芯片手册，但那款 IO APIC 过于老旧，版本号仅为 0x11
// 应该参考 Intel IO Controller Hub 的文档，ICH 也就是南桥，最新版为 ICH 10


// Intel® I/O Controller Hub 10 (Intel® ICH10) Family Datasheet
// 主要参考其中的 13.5


// 0x20 及以后版本的 IO APIC 才有 EOI 寄存器

// x2APIC 的 APIC ID 是 32-bit 整数，IO APIC 的 redirect entry 装不下
// 因此需要设置 Local APIC 的 LDR（logical destination register）
// 凡是 LDR 取值匹配的 Local APIC 都会接收到中断，即中断可以发给多个 CPU
// 但是 LDR 到底是多少 bit？如果是 8-bit，则取值可能冲突；如果 32-bit，怎么与 RED 比较？


// IO APIC 内存映射寄存器
#define IO_REG_SEL      0x00
#define IO_REG_WIN      0x10
#define IO_REG_EOI      0x40

// #define IOAPIC_IRQ_PIN  0x0020          // IRQ pin assertion

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

static uint32_t io_apic_read(size_t base, uint32_t reg) {
    *(volatile uint32_t *)(base + IO_REG_SEL) = reg;
    return *(volatile uint32_t *)(base + IO_REG_WIN);
}

static void io_apic_write(size_t base, uint32_t reg, uint32_t data) {
    *(volatile uint32_t *)(base + IO_REG_SEL) = reg;
    *(volatile uint32_t *)(base + IO_REG_WIN) = data;
}




void io_apic_send_eoi(ioapic_t *io, uint8_t vec) {
    if (io->ver >= 0x20) {
        *(volatile uint32_t *)(io->base + IO_REG_EOI) = vec;
    }
}





// 关于中断编号，有以下几种：
//  IRQ 编号，0~15，8259 定义
//  GSI 编号，从 0 开始，与 IRQ 一对一映射
//  RED 条目编号，具体到每一个 IO APIC
//  中断向量号，写在 RED 条目中，IO APIC 向 Local APIC 转发这个编号的中断消息
//      我们将 GSI 加上 0x40，就是中断号



// 寻找 GSI 所对应的 IO APIC
ioapic_t *io_apic_for_gsi(uint32_t gsi) {
    for (int i = 0; i < g_ioapic_num; ++i) {
        ioapic_t *io = &g_ioapics[i];
        if ((io->gsi_base <= gsi) && (gsi < io->gsi_base + io->ent_num)) {
            return io;
        }
        gsi -= io->ent_num;
    }

    klog("warning: cannot find IO APIC for gsi %d\n", gsi);
    return NULL;
}

void io_apic_set_red(uint32_t gsi, uint32_t hi, uint32_t lo) {
    ioapic_t *io = io_apic_for_gsi(gsi);
    if (NULL == io) {
        return;
    }

    gsi -= io->gsi_base;
    io_apic_write(io->base, IOAPIC_RED_H(gsi), hi);
    io_apic_write(io->base, IOAPIC_RED_L(gsi), lo);
}

// void io_apic_set_gsi(uint32_t gsi, int cpu, int vec) {
//     // 获取目标 CPU 的 LDR
//     uint32_t hi = (uint32_t)cpu << 24;
//     uint32_t lo = 0;
//     io_apic_set_red(gsi, hi, lo);
// }

void io_apic_mask_gsi(uint32_t gsi) {
    ioapic_t *io = io_apic_for_gsi(gsi);
    if (NULL == io) {
        return;
    }

    gsi -= io->gsi_base;
    uint32_t red_lo = io_apic_read(io->base, IOAPIC_RED_L(gsi));
    red_lo |= IOAPIC_INT_MASK;
    io_apic_write(io->base, IOAPIC_RED_L(gsi), red_lo);
}

void io_apic_unmask_gsi(uint32_t gsi) {
    ioapic_t *io = io_apic_for_gsi(gsi);
    if (NULL == io) {
        return;
    }

    gsi -= io->gsi_base;
    klog("unmasking redirect %d on IO APIC %d\n", gsi,  io->apic_id);
    uint32_t red_lo = io_apic_read(io->base, IOAPIC_RED_L(gsi));
    red_lo &= ~IOAPIC_INT_MASK;
    io_apic_write(io->base, IOAPIC_RED_L(gsi), red_lo);
}

INIT_TEXT void io_apic_init(ioapic_t *io) {
    io->base = io->address + DIRECT_MAP_ADDR;

    uint32_t id = io_apic_read(io->base, IOAPIC_ID);
    uint32_t ver = io_apic_read(io->base, IOAPIC_VER);

    if (id != io->apic_id) {
        klog("warning: IO APIC id %u different from MADT (%u)\n", id, io->apic_id);
    }

    io->ver = ver & 0xff;
    io->ent_num = ((ver >> 16) & 0xff) + 1;
    klog("IO APIC id=%u, madt-id=%d, ver=%u, ent_num=%d\n", id, io->apic_id, io->ver, io->ent_num);
    if (ver & (1 << 15)) {
        klog("no Pin Assertion Register\n");
    }
}


// 默认禁用所有硬件中断，按需开启
INIT_TEXT void io_apic_init_all() {
    for (int i = 0; i < g_ioapic_num; ++i) {
        ioapic_t *io = &g_ioapics[i];
        io_apic_init(io);

        int ent = 0;
        if (0 == i) {
            for (; ent < 16; ++ent) {
                uint32_t lo = IOAPIC_DM_LOWEST | IOAPIC_LOGICAL;
                lo |= get_gsi_trigmode(ent) ? IOAPIC_EDGE : IOAPIC_LEVEL;
                lo |= get_gsi_polarity(ent) ? IOAPIC_HIGH : IOAPIC_LOW;
                lo |= (ent + VEC_GSI_BASE) & IOAPIC_VEC_MASK;
                lo |= IOAPIC_INT_MASK;

                io_apic_write(io->base, IOAPIC_RED_H(ent), 0xff000000);
                // io_apic_write(io->base, IOAPIC_RED_H(ent), 0xfe000000); // 发送给 CPU0 之外的任意 CPU
                io_apic_write(io->base, IOAPIC_RED_L(ent), lo);
            }
        }

        // IRQ 之后的硬件中断，level-triggered，active low
        for (; ent < io->ent_num; ++ent) {
            uint32_t lo = IOAPIC_DM_LOWEST | IOAPIC_LOGICAL | IOAPIC_LEVEL | IOAPIC_LOW;
            lo |= (io->gsi_base + ent + VEC_GSI_BASE) & IOAPIC_VEC_MASK;
            lo |= IOAPIC_INT_MASK;

            io_apic_write(io->base, IOAPIC_RED_H(ent), 0xff000000);
            io_apic_write(io->base, IOAPIC_RED_L(ent), lo);
        }
    }
}
