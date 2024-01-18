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
#define IOAPIC_FIXED    0x00000000
#define IOAPIC_LOWEST   0x00000100
#define IOAPIC_SMI      0x00000200
#define IOAPIC_NMI      0x00000400
#define IOAPIC_INIT     0x00000500
#define IOAPIC_EXTINT   0x00000700
#define IOAPIC_VEC_MASK 0x000000ff

static uint32_t io_apic_read(size_t base, uint32_t reg) {
    *(volatile uint32_t *)(base + IO_REG_SEL) = reg;
    return *(volatile uint32_t *)(base + IO_REG_WIN);
}

static void io_apic_write(size_t base, uint32_t reg, uint32_t data) {
    *(volatile uint32_t *)(base + IO_REG_SEL) = reg;
    *(volatile uint32_t *)(base + IO_REG_WIN) = data;
}


INIT_TEXT void io_apic_init_all() {
    for (int i = 0; i < g_ioapic_num; ++i) {
        size_t base = g_ioapics[i].address + DIRECT_MAP_ADDR;
        uint32_t ver = io_apic_read(base, IOAPIC_VER);
        klog("IO APIC %d, ver=%d, ent_num=%d\n",
            i, ver & 0xff, (ver >> 16) & 0xff);
    }
}
