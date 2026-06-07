#ifndef ARCH_X86_64_ACPI_MCFG_H
#define ARCH_X86_64_ACPI_MCFG_H

#include "acpi.h"

typedef struct ecam {
    uint64_t base_address;      // ECAM MMIO 物理基地址
    uint16_t segment_group;     // PCI 段组号
    uint8_t  start_bus;         // 此段起始 bus 号
    uint8_t  end_bus;           // 此段结束 bus 号
    uint32_t reserved;
} PACKED ecam_t;

typedef struct mcfg {
    acpi_tbl_t  header;
    uint8_t     reserved[8];
    ecam_t      entries[0];
} PACKED mcfg_t;

#endif // ARCH_X86_64_ACPI_MCFG_H
