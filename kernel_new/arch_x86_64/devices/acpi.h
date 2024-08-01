#ifndef ACPI_H
#define ACPI_H

#include <common.h>

typedef struct acpi_tbl {
    char        signature[4];
    uint32_t    length;
    uint8_t     revision;
    uint8_t     checksum;
    char        oem_id[6];
    char        oem_table_id[8];
    uint32_t    oem_revision;
    uint32_t    creator_id;
    uint32_t    creator_revision;
} PACKED acpi_tbl_t;

INIT_TEXT size_t acpi_probe_rsdp();
INIT_TEXT void acpi_parse_rsdp(size_t rsdp);

void acpi_show_tables();
acpi_tbl_t *acpi_find_table(const char sig[4]);

#endif // ACPI_H
