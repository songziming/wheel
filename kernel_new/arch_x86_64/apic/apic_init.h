#ifndef APIC_INIT_H
#define APIC_INIT_H

#include <devices/acpi_madt.h>

INIT_TEXT void parse_madt(madt_t *madt);

#endif // APIC_INIT_H
