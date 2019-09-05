#include <wheel.h>

// extern driver functions
extern void ata_probe(u8 bus, u8 dev, u8 func);

#define PCI_ADDR    0x0cf8
#define PCI_DATA    0x0cfc

u32 pci_read(u8 bus, u8 dev, u8 func, u8 reg) {
    u32 addr = (((u32) bus  << 16) & 0x00ff0000U)
             | (((u32) dev  << 11) & 0x0000f800U)
             | (((u32) func <<  8) & 0x00000700U)
             | ( (u32) reg         & 0x000000fcU)
             | 0x80000000U;
    out32(PCI_ADDR, addr);
    return in32(PCI_DATA);
}

void pci_write(u8 bus, u8 dev, u8 func, u8 reg, u32 data) {
    u32 addr = (((u32) bus  << 16) & 0x00ff0000U)
             | (((u32) dev  << 11) & 0x0000f800U)
             | (((u32) func <<  8) & 0x00000700U)
             | ( (u32) reg         & 0x000000fcU)
             | 0x80000000U;
    out32(PCI_ADDR, addr);
    out32(PCI_DATA, data);
}

//------------------------------------------------------------------------------

static void pci_check_func(u8 bus, u8 dev, u8 func);
static void pci_check_dev (u8 bus, u8 dev);
static void pci_check_bus (u8 bus);

static void handle_pci_bridge(u8 bus, u8 dev, u8 func) {
    u8 sub = (pci_read(bus, dev, func, 24) >> 8) & 0xff;
    dbg_print("[pci] probine sub bus no.%d.\n", sub);
    pci_check_bus(sub);
}

static void pci_check_func(u8 bus, u8 dev, u8 func) {
    // u32 reg0   = pci_read(bus, dev, func, 0);
    // u16 vendor =  reg0        & 0xffff;
    // u16 device = (reg0 >> 16) & 0xffff;

    u32 reg2  = pci_read(bus, dev, func, 8);
    u16 ccode = (reg2 >> 16) & 0xffff;  // base and sub class code
    u8  prog  = (reg2 >>  8) & 0xff;    // programming interface

    // TODO: allow modules to register pci driver rather than hard code
    switch (ccode) {
    case 0x0101:        // IDE controller
        // dbg_print("[pci] IDE controller.\n");
        ata_probe(bus, dev, func);
        break;
    case 0x0200:        // ethernet controller
        // dbg_print("[pci] ethernet controller.\n");
        break;
    case 0x0300:        // VGA compatible controller
        // dbg_print("[pci] vga controller.\n");
        break;
    case 0x0600:        // host bridge
        // dbg_print("[pci] host bridge.\n");
        break;
    case 0x0601:        // ISA bridge
        // dbg_print("[pci] ISA bridge.\n");
        break;
    case 0x0602:
        // dbg_print("[pci] EISA bridge.\n");
        break;
    case 0x0603:
        // dbg_print("[pci] MCA bridge.\n");
        break;
    case 0x0604:        // PCI-to-PCI bridge
    case 0x0609:
        handle_pci_bridge(bus, dev, func);
        break;
    default:
        dbg_print("[pci] unknown pci device 0x%04x, prog if %d.\n", ccode, prog);
        break;
    }
}

static void pci_check_dev(u8 bus, u8 dev) {
    if (0xffff == (pci_read(bus, dev, 0, 0) & 0xffff)) {
        return;
    }
    pci_check_func(bus, dev, 0);

    // if this is multi function device, check remaining functions
    if (0 != ((pci_read(bus, dev, 0, 12) >> 16) & 0x80)) {
        for (u8 f = 1; f < 8; ++f) {
            if (0xffff == (pci_read(bus, dev, f, 0) & 0xffff)) {
                continue;
            }
            pci_check_func(bus, dev, f);
        }
    }
}

static void pci_check_bus(u8 bus) {
    for (u8 d = 0; d < 32; ++d) {
        pci_check_dev(bus, d);
    }
}

void pci_probe_all() {
    if (NULL != acpi_mcfg) {
        dbg_print("PCI Express present, we can use mapped memory.\n");
    }

    pci_check_bus(0);
}
