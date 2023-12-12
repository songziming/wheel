#ifndef DEV_ACPI_MADT_H
#define DEV_ACPI_MADT_H

#include "acpi.h"


typedef struct madt {
    acpi_tbl_t  header;
    uint32_t    loapic_addr;
    uint32_t    flags;
} PACKED madt_t;

#define MADT_FLAG_PCAT_COMPAT               1

#define MADT_TYPE_LOCAL_APIC                0
#define MADT_TYPE_IO_APIC                   1
#define MADT_TYPE_INTERRUPT_OVERRIDE        2
#define MADT_TYPE_NMI_SOURCE                3
#define MADT_TYPE_LOCAL_APIC_NMI            4
#define MADT_TYPE_LOCAL_APIC_OVERRIDE       5
#define MADT_TYPE_IO_SAPIC                  6   //
#define MADT_TYPE_LOCAL_SAPIC               7   // 这三个仅用于安腾
#define MADT_TYPE_PLATFORM_INTERRUPT_SOURCE 8   //
#define MADT_TYPE_LOCAL_X2APIC              9
#define MADT_TYPE_LOCAL_X2APIC_NMI          10
#define MADT_TYPE_GENERIC_INTERRUPT         11
#define MADT_TYPE_GENERIC_DISTRIBUTOR       12
#define MADT_TYPE_GENERIC_MSI_FRAME         13
#define MADT_TYPE_GENERIC_REDISTRIBUTOR     14
#define MADT_TYPE_GENERIC_TRANSLATOR        15
#define MADT_TYPE_RESERVED                  16

typedef struct acpi_subtbl {
    uint8_t     type;
    uint8_t     length;
} PACKED acpi_subtbl_t;

// type = 0, Processor Local APIC
typedef struct madt_loapic {
    acpi_subtbl_t header;
    uint8_t       processor_id;
    uint8_t       id;
    uint32_t      loapic_flags;
} PACKED madt_loapic_t;

// type = 1, I/O APIC
typedef struct madt_ioapic {
    acpi_subtbl_t header;
    uint8_t       id;
    uint8_t       reserved;
    uint32_t      address;
    uint32_t      gsi_base;
} PACKED madt_ioapic_t;

// type = 2, Interrupt Source Override
typedef struct madt_int_override {
    acpi_subtbl_t header;
    uint8_t       bus;          // always 0, meaning ISA
    uint8_t       source;
    uint32_t      gsi;
    uint16_t      inti_flags;
} PACKED madt_int_override_t;

// type = 3, Non-Maskable Interrupt (NMI) Source
typedef struct madt_nmi {
    acpi_subtbl_t header;
    uint16_t      inti_flags;
    uint32_t      gsi;
} PACKED madt_nmi_t;

#define POLARITY_MASK       3
#define POLARITY_CONFIRM    0
#define POLARITY_HIGH       1   // active high
#define POLARITY_LOW        3   // active low
#define TRIGMODE_MASK       12
#define TRIGMODE_CONFIRM    0
#define TRIGMODE_EDGE       4   // edge-triggered
#define TRIGMODE_LEVEL      12  // level-triggered

// type = 4, Local APIC Non Maskable Interrupts
typedef struct madt_loapic_nmi {
    acpi_subtbl_t header;
    uint8_t       processor_id;
    uint16_t      inti_flags;
    uint8_t       lint;
} PACKED madt_loapic_nmi_t;

// type = 5, Address Override
typedef struct madt_loapic_override {
    acpi_subtbl_t header;
    uint16_t      reserved;
    uint64_t      address;
} PACKED madt_loapic_override_t;

// type = 8, Platform Interrupt Source
typedef struct madt_int_source {
    acpi_subtbl_t header;
    uint16_t      inti_flags;
    uint8_t       type;
    uint8_t       id;
    uint8_t       eid;
    uint8_t       io_sapic_vector;
    uint32_t      global_irq;
    uint32_t      flags;
} PACKED madt_int_source_t;

// type = 9, Processor Local x2APIC
typedef struct madt_lox2apic {
    acpi_subtbl_t header;
    uint16_t      reserved;
    uint32_t      id;
    uint32_t      loapic_flags;
    uint32_t      processor_id;
} PACKED madt_lox2apic_t;

// type = 10, Local x2APIC NMI
typedef struct madt_lox2apic_nmi {
    acpi_subtbl_t header;
    uint16_t      inti_flags;
    uint32_t      processor_id;
    uint8_t       lint;
    uint8_t       reserved[3];
} PACKED madt_lox2apic_nmi_t;

#endif // DEV_ACPI_MADT_H
