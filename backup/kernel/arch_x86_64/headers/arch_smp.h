#ifndef ARCH_SMP_H
#define ARCH_SMP_H

#include <def.h>
#include <dev/acpi_madt.h>

typedef struct loapic {
    uint32_t apic_id;
    uint32_t processor_id;
    uint32_t flags;

    uint16_t cluster_id;
    uint16_t logical_id;
} loapic_t;

typedef struct ioapic {
    uint32_t apic_id;
    uint32_t gsi_base;
    size_t   address;   // 物理地址

    size_t   base;  // 虚拟地址
    uint8_t  ver;
    int      ent_num;
} ioapic_t;

extern CONST size_t g_loapic_addr;
extern CONST int    g_loapic_num;
extern CONST int    g_ioapic_num;

extern CONST loapic_t *g_loapics;
extern CONST ioapic_t *g_ioapics;

INIT_TEXT void parse_madt(const madt_t *madt);

INIT_TEXT int requires_int_remap();

INIT_TEXT int get_nmi_lint(int cpu);

INIT_TEXT int get_gsi_for_irq(int irq);
INIT_TEXT int get_gsi_trigmode(int gsi); // edge=1, level=0
INIT_TEXT int get_gsi_polarity(int gsi); // high=1, low=0

#endif // ARCH_SMP_H
