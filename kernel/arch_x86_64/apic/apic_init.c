#include "apic.h"
#include <debug.h>



CONST int    g_loapic_num;
CONST size_t g_loapic_addr;

CONST int g_ioapic_num;

// TODO move to standalone file
INIT_TEXT void parse_madt(madt_t *madt) {
    g_loapic_addr = (size_t)madt->loapic_addr;
    logk("local apic base = %zx\n", g_loapic_addr);

    // 统计 local apic、io apic 个数，irq-gsi 映射表长度
    g_loapic_num = 0;
    g_ioapic_num = 0;
    for (size_t i = sizeof(madt_t); i < madt->header.length;) {
        acpi_subtbl_t *sub = (acpi_subtbl_t*)((size_t)madt + i);
        i += sub->length;

        switch (sub->type) {
        case MADT_TYPE_LOCAL_APIC_OVERRIDE:
            g_loapic_addr = ((madt_loapic_override_t*)sub)->address;
            break;
        case MADT_TYPE_LOCAL_APIC:
            g_loapic_num += ((madt_loapic_t*)sub)->loapic_flags & 1;
            break;
        case MADT_TYPE_LOCAL_X2APIC:
            g_loapic_num += ((madt_lox2apic_t*)sub)->loapic_flags & 1;
            break;
        case MADT_TYPE_IO_APIC:
            ++g_ioapic_num;
            break;
        }
    }
}
