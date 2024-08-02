#ifndef SMP_H
#define SMP_H

#include <common.h>
#include <devices/acpi_madt.h>

typedef struct loapic {
    uint32_t apic_id;
    uint32_t processor_id;
    uint32_t flags;

    // uint16_t cluster_id;
    // uint16_t logical_id;
} loapic_t;

typedef struct ioapic {
    uint32_t apic_id;
    uint32_t gsi_base;
    size_t   address;   // 物理地址

    // size_t   base;  // 虚拟地址
    uint8_t  ver;
    int      ent_num;
} ioapic_t;

INIT_TEXT void parse_madt(madt_t *madt);

#endif // SMP_H
