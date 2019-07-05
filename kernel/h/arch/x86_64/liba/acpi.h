#ifndef ARCH_X86_64_LIBA_ACPI_H
#define ARCH_X86_64_LIBA_ACPI_H

#include <base.h>

//------------------------------------------------------------------------------
// top-level tables

typedef struct acpi_tbl {
    u32    signature;
    u32    length;
    u8     revision;
    u8     checksum;
    char   oem_id[6];
    char   oem_table_id[8];
    u32    oem_revision;
    u32    asl_compiler_id;
    u32    asl_compiler_revision;
} __PACKED acpi_tbl_t;

typedef struct acpi_subtbl {
    u8     type;
    u8     length;
} __PACKED acpi_subtbl_t;

typedef struct acpi_rsdp {
    u64     signature;
    u8      checksum;
    char    oem_id[6];
    u8      revision;
    u32     rsdt_addr;
    u32     length;         // this and following field are v2 only
    u64     xsdt_addr;
    u8      checksum_ext;
    u8      reserved[3];
} __PACKED acpi_rsdp_t;

typedef struct acpi_rsdt {
    acpi_tbl_t  header;
    u32         entries[0];
} __PACKED acpi_rsdt_t;

typedef struct acpi_xsdt {
    acpi_tbl_t  header;
    u64         entries[0];
} __PACKED acpi_xsdt_t;

//------------------------------------------------------------------------------
// madt

typedef struct madt {
    acpi_tbl_t  header;
    u32         loapic_addr;
    u32         flags;
} __PACKED madt_t;

#define MADT_TYPE_LOCAL_APIC             0
#define MADT_TYPE_IO_APIC                1
#define MADT_TYPE_INTERRUPT_OVERRIDE     2
#define MADT_TYPE_NMI_SOURCE             3
#define MADT_TYPE_LOCAL_APIC_NMI         4
#define MADT_TYPE_LOCAL_APIC_OVERRIDE    5
#define MADT_TYPE_IO_SAPIC               6
#define MADT_TYPE_LOCAL_SAPIC            7
#define MADT_TYPE_INTERRUPT_SOURCE       8
#define MADT_TYPE_LOCAL_X2APIC           9
#define MADT_TYPE_LOCAL_X2APIC_NMI      10
#define MADT_TYPE_GENERIC_INTERRUPT     11
#define MADT_TYPE_GENERIC_DISTRIBUTOR   12
#define MADT_TYPE_GENERIC_MSI_FRAME     13
#define MADT_TYPE_GENERIC_REDISTRIBUTOR 14
#define MADT_TYPE_GENERIC_TRANSLATOR    15
#define MADT_TYPE_RESERVED              16

// type = 0, Local APIC
typedef struct madt_loapic {
    acpi_subtbl_t header;
    u8            processor_id;
    u8            id;
    u8            loapic_flags;
} __PACKED madt_loapic_t;

// type = 1, IO APIC
typedef struct madt_ioapic {
    acpi_subtbl_t header;
    u8            id;
    u8            reserved;
    u32           address;
    u32           global_irq_base;
} __PACKED madt_ioapic_t;

// type = 2, Interrupt Override
typedef struct madt_int_override {
    acpi_subtbl_t header;
    u8            bus;          // always 0, meaning ISA
    u8            source_irq;
    u32           global_irq;
#define POLARITY_MASK       3
#define POLARITY_CONFIRM    0   // confirm to the specifications of the bus
                                // e.g. E-ISA is active-low, level-triggered
#define POLARITY_HIGH       1   // active high
#define POLARITY_LOW        3   // active low
#define TRIGMODE_MASK      12
#define TRIGMODE_CONFIRM    0   // conforms to specifications of the bus
                                // e.g. ISA is edge-triggered
#define TRIGMODE_EDGE       4   // edge-triggered
#define TRIGMODE_LEVEL     12   // level-triggered
    u16           inti_flags;
} __PACKED madt_int_override_t;

// type = 4, Local APIC Non Maskable Interrupts
typedef struct madt_loapic_mni {
    acpi_subtbl_t header;
    u8            processor_id;
    u16           flags;
    u8            lint;
} __PACKED madt_loapic_mni_t;

// type = 5, Address Override
typedef struct madt_loapic_override {
    acpi_subtbl_t header;
    u16           reserved;
    u64           address;
} __PACKED madt_loapic_override_t;

// type = 8, Platform Interrupt Source
typedef struct madt_int_source {
    acpi_subtbl_t header;
    u16           inti_flags;
    u8            type;
    u8            id;
    u8            eid;
    u8            io_sapic_vector;
    u32           global_irq;
    u32           flags;
} __PACKED madt_int_source_t;

//------------------------------------------------------------------------------
// mcfg, for pci-express

typedef struct pci_conf {
    u64         address;
    u16         seg_group;
    u8          bus_num_start;
    u8          bus_num_end;
    u32         reserved;
} __PACKED pci_conf_t;

typedef struct mcfg {
    acpi_tbl_t  header;
    u32         reserved;
    pci_conf_t  entries[0];
} __PACKED mcfg_t;

//------------------------------------------------------------------------------
// hpet and fadt

typedef struct hpet {
    acpi_tbl_t  header;
    u32         other;  // TODO: fill the rest
} __PACKED hpet_t;

typedef struct fadt {
    acpi_tbl_t  header;
    u32         other;  // TODO: fill the rest
} __PACKED fadt_t;

//------------------------------------------------------------------------------
// global variables and function

extern madt_t * acpi_madt;
extern mcfg_t * acpi_mcfg;
extern fadt_t * acpi_fadt;
extern hpet_t * acpi_hpet;

// requires: nothing
extern __INIT void acpi_tbl_init();

#endif // ARCH_X86_64_LIBA_ACPI_H
