#ifndef DEV_ACPI_HPET_H
#define DEV_ACPI_HPET_H

#include "acpi_base.h"

typedef struct hpet {
    acpi_tbl_t  header;
    uint32_t    blockid;    // event timer block id
    uint32_t    address[3];
    uint8_t     number;
    uint16_t    min_tick;
} PACKED hpet_t;

#endif // DEV_ACPI_HPET_H
