#ifndef ARCH_X86_64_APIC_APIC_H
#define ARCH_X86_64_APIC_APIC_H

#include <acpi/madt.h>


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
    size_t   address;   // mmio 物理地址
    uint8_t  ver;
    int      red_num;   // 重定位条目数量
} ioapic_t;


// local APIC
extern int    g_loapic_num;
extern size_t g_loapic_addr;
extern loapic_t *g_loapics;

INIT_TEXT void loapic_parse(loapic_t *dst, const madt_loapic_t *tbl);
INIT_TEXT void loapic_parse_x2(loapic_t *dst, const madt_lox2apic_t *tbl);
INIT_TEXT void loapic_init(int idx);
void loapic_show();

// IO APIC
extern int       g_ioapic_num;
extern ioapic_t *g_ioapics;
extern uint8_t   g_irq_max;
extern uint32_t  g_gsi_max;
extern uint32_t *g_irq_to_gsi;
extern uint8_t  *g_gsi_to_irq;
extern uint8_t  *g_gsi_modes; // 记录该中断的 polarity、trigger level
#define GSI_MODE_EDGE 1 // edge-triggered
#define GSI_MODE_HIGH 2 // active-high

INIT_TEXT void ioapic_parse(ioapic_t *dst, const madt_ioapic_t *tbl);

// init func
INIT_TEXT void parse_madt(madt_t *madt);

#endif // ARCH_X86_64_APIC_APIC_H
