#include "arch_pci.h"

#include "cpu/rw.h"
#include "devices/acpi.h"

#include <library/debug.h>
#include <library/string.h>

#include <services/pci.h>


typedef struct pci_conf {
    uint64_t    addr;
    uint16_t    seg;
    uint8_t     bus_start;
    uint8_t     bus_end;
    uint32_t    reserved;
} PACKED pci_conf_t;

typedef struct mcfg {
    acpi_tbl_t  header;
    uint64_t    reserved;
    pci_conf_t  spaces[0];
} PACKED mcfg_t;



//------------------------------------------------------------------------------
// legacy PCI configuration space
//------------------------------------------------------------------------------

#define CONFIG_ADDR 0xcf8
#define CONFIG_DATA 0xcfc

static uint32_t pci_read(uint8_t bus, uint8_t dev, uint8_t func, uint8_t reg) {
    ASSERT(dev < 32);
    ASSERT(func < 8);
    ASSERT(0 == (reg & 3));

    uint32_t addr = ((uint32_t)bus  << 16)
                  | ((uint32_t)dev  << 11)
                  | ((uint32_t)func <<  8)
                  |  (uint32_t)reg
                  | 0x80000000U;
    out32(CONFIG_ADDR, addr);
    return in32(CONFIG_DATA);
}

static void pci_write(uint8_t bus, uint8_t dev, uint8_t func, uint8_t reg, uint32_t data) {
    ASSERT(dev < 32);
    ASSERT(func < 8);
    ASSERT(0 == (reg & 3));

    uint32_t addr = ((uint32_t)bus  << 16)
                  | ((uint32_t)dev  << 11)
                  | ((uint32_t)func <<  8)
                  |  (uint32_t)reg
                  | 0x80000000U;
    out32(CONFIG_ADDR, addr);
    out32(CONFIG_DATA, data);
}





INIT_TEXT void arch_pci_lib_init() {
    mcfg_t *mcfg = (mcfg_t *)acpi_table_find("MCFG", 0);
    if (mcfg) {
        int num = (mcfg->header.length - sizeof(acpi_tbl_t)) / sizeof(pci_conf_t);
        log("has PCIe support! %d conf spaces\n", num);
        for (int i = 0; i < num; ++i) {
            pci_conf_t *conf = &mcfg->spaces[i];
            log("- PCIe conf space at 0x%zx, seg=%d, bus=%d-%d\n",
                conf->addr, conf->seg, conf->bus_start, conf->bus_end);
        }
    }
    pci_lib_init(pci_read, pci_write);
    pci_probe();
}