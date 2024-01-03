#ifndef ARCH_SMP_H
#define ARCH_SMP_H

#include <def.h>
#include <dev/acpi_madt.h>

typedef struct loapic {
    uint32_t apic_id;
    uint32_t processor_id;
    uint32_t flags;
} loapic_t;

typedef struct ioapic {
    uint32_t apic_id;
    uint32_t gsi_base;
    size_t   address;   // 物理地址
} ioapic_t;

extern CONST size_t g_loapic_addr;
extern CONST int    g_loapic_num;
extern CONST int    g_ioapic_num;

extern CONST loapic_t *g_loapics;
extern CONST ioapic_t *g_ioapics;

INIT_TEXT int nmi_lint(int cpu);
INIT_TEXT void parse_madt(const madt_t *madt);

#endif // ARCH_SMP_H
