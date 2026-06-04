#ifndef ARCH_X86_64_APIC_APIC_H
#define ARCH_X86_64_APIC_APIC_H

#include <acpi/madt.h>

typedef struct loapic {
    uint32_t apic_id;
    uint32_t processor_id;
    uint32_t flags;
    uint16_t cluster_id;
    uint16_t logical_id;
    int16_t  nmi_lint;  // NMI 连接到这个 Local APIC 的哪个 lint
    uint16_t nmi_flags;
} loapic_t;

typedef struct ioapic {
    uint32_t apic_id;
    uint32_t gsi_base;
    size_t   addr;   // mmio 物理地址
    uint8_t  ver;
    int      red_num;   // 重定位条目数量
} ioapic_t;


// local APIC data
extern int       g_loapic_num;
extern size_t    g_loapic_addr;
extern loapic_t *g_loapics;

// IO APIC data
extern int       g_ioapic_num;
extern ioapic_t *g_ioapics;


// top func
INIT_TEXT int irq_to_gsi(uint8_t irq);
INIT_TEXT int gsi_is_edge(uint32_t gsi);
INIT_TEXT int gsi_is_high(uint32_t gsi);
INIT_TEXT void parse_madt(madt_t *madt);

// local apic func
INIT_TEXT void loapic_init();
INIT_TEXT void loapic_init_local();
void loapic_show();
void loapic_send_eoi();
INIT_TEXT void loapic_send_init(int cpu);
INIT_TEXT void loapic_send_sipi(int cpu, int vec);

// local apic timer func
INIT_TEXT void loapic_timer_calibrate();
void loapic_timer_set_periodic(int freq);
void loapic_timer_busywait(int us);

// IO apic func
INIT_TEXT void ioapic_init();
void ioapic_mask_gsi(uint32_t gsi);
void ioapic_unmask_gsi(uint32_t gsi);
void ioapic_route_gsi(uint32_t gsi, int cpu, uint8_t vec);
void ioapic_send_eoi(int vec); // 只有 level-triggered 中断需要调用

#endif // ARCH_X86_64_APIC_APIC_H
