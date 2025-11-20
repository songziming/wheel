#include "apic.h"




CONST int g_ioapic_num;
CONST ioapic_t *g_ioapics;

CONST uint8_t   g_irq_max = 0;
CONST uint32_t  g_gsi_max = 0;

CONST uint32_t *g_irq_to_gsi = NULL;
CONST uint8_t  *g_gsi_to_irq = NULL;
CONST uint8_t  *g_gsi_modes = NULL; // 记录该中断的 polarity、trigger level


INIT_TEXT void ioapic_parse(ioapic_t *dst, const madt_ioapic_t *tbl) {
    dst->apic_id  = tbl->id;
    dst->gsi_base = tbl->gsi_base;
    dst->address  = tbl->address;
}

// INIT_TEXT void override_int(madt_int_override_t *tbl) {
//     ASSERT(g_irq_num > 0);
//     ASSERT(g_gsi_num > 0);

//     g_irq_to_gsi[tbl->source] = tbl->gsi;
//     g_gsi_to_irq[tbl->gsi] = tbl->source;

//     if (TRIGMODE_LEVEL == (TRIGMODE_MASK & tbl->inti_flags)) {
//         g_gsi_modes[tbl->gsi].edge = 0;
//     }
//     if (POLARITY_LOW == (POLARITY_MASK & tbl->inti_flags)) {
//         g_gsi_modes[tbl->gsi].high = 0;
//     }
// }