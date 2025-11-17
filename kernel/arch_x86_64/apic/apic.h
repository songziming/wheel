#ifndef ARCH_X86_64_APIC_APIC_H
#define ARCH_X86_64_APIC_APIC_H

#include <acpi/madt.h>

// local APIC
extern int    g_loapic_num;
extern size_t g_loapic_addr;

// IO APIC
extern int  g_ioapic_num;

// init func
INIT_TEXT void parse_madt(madt_t *madt);

#endif // ARCH_X86_64_APIC_APIC_H
