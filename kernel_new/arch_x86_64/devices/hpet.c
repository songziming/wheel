#include "acpi.h"
#include <arch_impl.h>
#include <library/debug.h>
#include <memory/early_alloc.h>



// 高精度时钟，可用来模拟 IRQ 0 (PIT) 和 IRQ 8 (RTC)
// 如果发现了 HPET，就视为最准确的时钟源，用来校准 Local APIC Timer


typedef struct addr {
    uint8_t space_id;    // 0 - system memory, 1 - system I/O
    uint8_t register_bit_width;
    uint8_t register_bit_offset;
    uint8_t reserved;
    uint64_t address;
} PACKED addr_t;

typedef struct hpet {
    acpi_tbl_t  header;

    uint8_t hardware_rev_id;
    uint8_t comparator_count    : 5;
    uint8_t counter_size        : 1;
    uint8_t reserved            : 1;
    uint8_t legacy_replacement  : 1;
    uint16_t pci_vendor_id;
    addr_t   address;
    uint8_t  hpet_number;
    uint16_t minimum_tick;
    uint8_t  page_protection;
} PACKED hpet_t;

typedef struct hpet_dev {
    uint64_t    base;   // 寄存器映射的虚拟地址
    uint64_t    freq;
    uint32_t    period; // 单位是飞秒（10^-15）
} hpet_dev_t;


static CONST int        g_hpet_num = 0;
static CONST hpet_dev_t *g_hpet_devs = NULL;



//------------------------------------------------------------------------------
// 寄存器读写
//------------------------------------------------------------------------------

// HPET 寄存器都是 64-bit，占据八个字节
#define GENERAL_CAP_ID      0x00
#define GENERAL_CONF        0x10
#define GENERAL_INT_STATE   0x20
#define TIMER_CONF_CAP(n)   (0x100 + ((n) << 5))
#define TIMER_COMP_VAL(n)   (0x108 + ((n) << 5))
#define TIMER_FSB_ROUTE(n)  (0x110 + ((n) << 5))
#define MAIN_COUNTER_VAL    0xf0

// general capabilities register fields
#define COUNT_SIZE_CAP (1UL << 13)
#define LEG_RT_CAP (1UL << 15)

// general configuration register fields
#define ENABLE_CNF 1
#define LEG_RT_CNF 2

// timer configuration and capability register fields
#define INT_TYPE_CNF    0x02    // 0 表示电平触发，1 表示边沿触发
#define INT_ENB_CNF     0x04    // 中断开关
#define TYPE_CNF        0x08    // 周期模式开关（如果支持）
#define PER_INT_CAP     0x10    // （只读）支持周期模式
#define SIZE_CAP        0x20    // （只读）64-bit
#define VAL_SET_CNF

static uint64_t hpet_read(hpet_dev_t *dev, int reg) {
    return *(volatile uint64_t *)(dev->base + reg);
}

static void hpet_write(hpet_dev_t *dev, int reg, uint64_t val) {
    *(volatile uint64_t *)(dev->base + reg) = val;
}


//------------------------------------------------------------------------------
// 设备初始化
//------------------------------------------------------------------------------

static INIT_TEXT void hpet_dev_init(hpet_t *tbl, hpet_dev_t *dev) {
    dev->base = DIRECT_MAP_ADDR + tbl->address.address;

    hpet_write(dev, GENERAL_CONF, 0); // 确保时钟关闭

    uint64_t cap = hpet_read(dev, GENERAL_CAP_ID);
    dev->period = (uint32_t)(cap >> 32);
    log("period %ld\n", dev->period);

    uint64_t freq = 1000000000000000UL;
    freq += dev->period >> 1;
    freq /= dev->period;
    dev->freq = freq;

    if (cap & LEG_RT_CAP) {
        log("HPET capable of legacy replacement\n");
    }
    if (cap & COUNT_SIZE_CAP) {
        log("HPET capable of 64-bit mode\n");
    }

    int max_timer = (cap >> 8) & 0x1f; // comparator数量，应该和 ACPI 表中的相同
    for (int i = 0; i <= max_timer; ++i) {
        uint64_t confcap = hpet_read(dev, TIMER_CONF_CAP(i));
        log("  - comparator %d", i);
        if (confcap & PER_INT_CAP) {
            log(" periodic");
        }
        if (confcap & SIZE_CAP) {
            log(" 64-bit");
        }
        log("\n");
    }
}

INIT_TEXT void hpet_init() {
    g_hpet_num = acpi_table_count("HPET");
    if (0 == g_hpet_num) {
        return;
    }

    g_hpet_devs = early_alloc_rw(g_hpet_num * sizeof(hpet_dev_t));
    for (int i = 0; i < g_hpet_num; ++i) {
        hpet_t *tbl = (hpet_t *)acpi_table_find("HPET", i);
        hpet_dev_init(tbl, &g_hpet_devs[i]);
    }
}
