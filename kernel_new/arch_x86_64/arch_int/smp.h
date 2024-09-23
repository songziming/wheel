#ifndef SMP_H
#define SMP_H

#include <common.h>
#include <devices/acpi_madt.h>

INIT_TEXT void parse_madt(madt_t *madt);

#endif // SMP_H
