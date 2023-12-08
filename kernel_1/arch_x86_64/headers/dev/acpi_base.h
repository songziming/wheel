#ifndef DEV_ACPI_BASE_H
#define DEV_ACPI_BASE_H

#include <base.h>


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

typedef struct acpi_subtbl {
    uint8_t     type;
    uint8_t     length;
} PACKED acpi_subtbl_t;

typedef struct acpi_rsdp {
    uint64_t    signature;
    uint8_t     checksum;
    char        oem_id[6];
    uint8_t     revision;
    uint32_t    rsdt_addr;

    // 之后的字段属于 v2.0
    uint32_t    length;
    uint64_t    xsdt_addr;
    uint8_t     checksum2;
    uint8_t     reserved[3];
} PACKED acpi_rsdp_t;

typedef struct acpi_rsdt {
    acpi_tbl_t  header;
    uint32_t    entries[0];
} PACKED acpi_rsdt_t;

typedef struct acpi_xsdt {
    acpi_tbl_t  header;
    uint64_t    entries[0];
} PACKED acpi_xsdt_t;


INIT_TEXT acpi_rsdp_t *acpi_probe_rsdp();
INIT_TEXT void acpi_parse_rsdp(acpi_rsdp_t *rsdp);

acpi_tbl_t *acpi_get_table(const char sig[4]);

#endif // DEV_ACPI_BASE_H
