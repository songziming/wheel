#ifndef IOAPIC_H
#define IOAPIC_H

#include <devices/acpi_madt.h>

typedef struct ioapic {
    uint32_t apic_id;
    uint32_t gsi_base;
    size_t   address;   // mmio 物理地址

    uint8_t  ver;
    int      red_num;   // 重定位条目数量
} ioapic_t;

uint32_t irq_to_gsi(int irq);

void ioapic_mask_gsi(uint32_t gsi);
void ioapic_unmask_gsi(uint32_t gsi);
void ioapic_send_eoi(int vec);

INIT_TEXT void ioapic_alloc(int n, uint8_t irq_max, uint32_t gsi_max);
INIT_TEXT void ioapic_parse(int i, madt_ioapic_t *tbl);
INIT_TEXT void override_int(madt_int_override_t *tbl);
INIT_TEXT void ioapic_init_all();

#endif // IOAPIC_H
