#ifndef ARCH_API_P_H
#define ARCH_API_P_H

#include <arch_api.h>
#include <dev/acpi.h>

int arch_unwind(size_t *addrs, int max, uint64_t rbp);

INIT_TEXT void arch_pci_lib_init(acpi_tbl_t *mcfg);

INIT_TEXT void install_resched_handlers();

#endif // ARCH_API_P_H
