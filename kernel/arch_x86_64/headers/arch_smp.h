#ifndef ARCH_SMP_H
#define ARCH_SMP_H

#include <base.h>
#include <dev/acpi.h>

INIT_TEXT void parse_madt(madt_t *madt);

#endif // ARCH_SMP_H
