#include "hpet.h"
#include <arch_api.h>
#include <acpi/acpi.h>
#include <debug.h>

// WIP 高精度时钟，适合异构平台同步 tsc

typedef struct hpet {
    acpi_tbl_t  header;

    uint8_t hardware_rev_id;
    uint8_t comparator_count    : 5;
    uint8_t counter_size        : 1;
    uint8_t reserved            : 1;
    uint8_t legacy_replacement  : 1;
    uint16_t pci_vendor_id;
    struct {
        uint8_t space_id;    // 0 - system memory, 1 - system I/O
        uint8_t register_bit_width;
        uint8_t register_bit_offset;
        uint8_t reserved;
        uint64_t address;
    } address;
    uint8_t  hpet_number;
    uint16_t minimum_tick;
    uint8_t  page_protection;
} hpet_t;


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


// HPET 使用 MMIO
static uint64_t hpet_read(size_t base, int reg) {
    return *(volatile uint64_t*)(base + reg);
}
static void hpet_write(size_t base, int reg, uint64_t val) {
    *(volatile uint64_t*)(base + reg) = val;
}


INIT_TEXT void hpet_init() {
    hpet_t *tbl = (hpet_t*)acpi_table_find("HPET", 0);
    if (NULL == tbl) {
        logk("HPET not found!\n");
        return;
    }

    size_t base = (size_t)idmap_at(tbl->address.address);
    hpet_write(base, GENERAL_CONF, 0); // 确保时钟关闭

    uint64_t cap = hpet_read(base, GENERAL_CAP_ID);
    logk("HPET period is %ld\n", cap >> 32);
}
