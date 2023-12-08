#ifndef ARCH_SMP_H
#define ARCH_SMP_H

#include <base.h>
// #include <dev/acpi_madt.h>


typedef struct loapic {
    int      processor_id;  // 处理器编号（不保证连续）
    uint32_t apic_id;       // APIC 总线编号（不保证连续）
    uint32_t nmi_mask;      // 哪些 lint 被关联到了 NMI
} loapic_t;

typedef struct ioapic {
    uint32_t apic_id;
    int      gsi_base;      // 对应的第一个 GSI
    uint64_t addr;          // 内存映射地址（物理地址）
    uint64_t nmi_mask;      // 哪些 GSI 属于 NMI
} ioapic_t;

extern CONST size_t    g_loapic_addr;
extern CONST int       g_loapic_count;
extern CONST int       g_ioapic_count;
extern CONST loapic_t *g_loapics;
extern CONST ioapic_t *g_ioapics;

typedef struct madt madt_t;

INIT_TEXT void parse_madt(madt_t *tbl);

INIT_TEXT void gsbase_init(int idx);

#endif // ARCH_SMP_H
