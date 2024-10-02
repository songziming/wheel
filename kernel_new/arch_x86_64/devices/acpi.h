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

int acpi_table_count(const char sig[4]);
acpi_tbl_t *acpi_table_find(const char sig[4], int idx);
void acpi_tables_show();

#endif // ACPI_H
