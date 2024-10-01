#include "acpi.h"
#include <arch_impl.h>
#include <library/debug.h>



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


// HPET 寄存器都是 64-bit，占据八个字节
#define GENERAL_CAP_ID      0x00
#define GENERAL_CONF        0x10
#define GENERAL_INT_STATE   0x20
#define TIMER_CONF_CAP(n)   (0x100 + ((n) << 5))
#define TIMER_COMP_VAL(n)   (0x108 + ((n) << 5))
#define TIMER_FSB_ROUTE(n)  (0x110 + ((n) << 5))
#define MAIN_COUNTER_VAL    0xf0


static CONST uint64_t g_base = 0; // 映射的虚拟地址


static uint64_t hpet_read(int reg) {
    return *(volatile uint64_t *)(g_base + reg);
}

static void hpet_write(int reg, uint64_t val) {
    *(volatile uint64_t *)(g_base + reg) = val;
}


void hpet_init() {
    hpet_t *hpet = (hpet_t *)acpi_table_find("HPET");
    if (NULL == hpet) {
        log("HPET not found!\n");
        return;
    }

    // HPET 寄存器映射到内存空间
    g_base = DIRECT_MAP_ADDR + hpet->address.address;

    log("found HPET at %p\n", hpet);
    log("mapped to %s space address %zx\n",
        hpet->address.space_id ? "io" : "memory", hpet->address.address);
    log("%d comparators\n", hpet->comparator_count);

    uint64_t cap = hpet_read(GENERAL_CAP_ID);
    uint32_t period = cap >> 32; // 单位是飞秒（10^-15 s）
    if (cap & 0x8000) {
        log("HPET capable of legacy replacement\n");
    }
    if (cap & 0x2000) {
        log("HPET capable of 64-bit mode\n");
    }

    int max_timer = (cap >> 8) & 0x1f; // comparator数量，应该和 ACPI 表中的相同
    log("found %d timers\n", max_timer);
    ASSERT(max_timer == hpet->comparator_count);
}
