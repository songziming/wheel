#ifndef IOAPIC_H
#define IOAPIC_H

#include <common.h>

typedef struct ioapic {
    uint32_t apic_id;
    uint32_t gsi_base;
    size_t   address;   // mmio 物理地址

    uint8_t  ver;
    int      red_num;   // 重定位条目数量
} ioapic_t;

#endif // IOAPIC_H
