#ifndef ARCH_SMP_H
#define ARCH_SMP_H

#include <def.h>
#include <dev/acpi.h>

extern CONST size_t g_loapic_addr;
extern CONST int    g_loapic_num;
extern CONST int    g_ioapic_num;

INIT_TEXT void parse_madt(const madt_t *madt);

#endif // ARCH_SMP_H
