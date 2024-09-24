#ifndef INT_INIT_H
#define INT_INIT_H

#include <common.h>
#include <devices/acpi_madt.h>

INIT_TEXT void parse_madt(madt_t *madt);

INIT_TEXT void int_init();

#endif // INT_INIT_H
