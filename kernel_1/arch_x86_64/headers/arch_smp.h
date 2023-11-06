#ifndef ARCH_SMP_H
#define ARCH_SMP_H

#include <base.h>
#include <dev/acpi.h>

extern CONST int g_loapic_num;
extern CONST int g_ioapic_num;

INIT_TEXT void parse_madt(madt_t *madt);


// extern CONST size_t *g_pcpu_offsets;
// extern PCPU_BSS int g_cpu_index;

INIT_TEXT void gsbase_init(int idx);

#endif // ARCH_SMP_H
