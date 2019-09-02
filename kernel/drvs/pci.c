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

static u16 get_vendor_id(u8 bus, u8 dev, u8 func) {
    return pci_read(bus, dev, func, 0) & 0xffff;
}

static u8 get_header_type(u8 bus, u8 dev, u8 func) {
    return (pci_read(bus, dev, func, 12) >> 16) & 0xff;
}

static void pci_check_func(u8 bus, u8 dev, u8 func) {
    // u32 reg0 = pci_read(bus, dev, func, 0);
    u32 reg2 = pci_read(bus, dev, func, 8);
    // u16 vendor =  reg0        & 0xffff;
    // u16 device = (reg0 >> 16) & 0xffff;
    u16 ccode  = (reg2 >> 16) & 0xffff;   // base and sub class code
    u8  prog   = (reg2 >>  8) & 0xff;     // programming interface

    // TODO: allow modules to register pci driver rather than hard code
    switch (ccode) {
    case 0x0101:        // IDE controller
        switch (prog) {
        case 0x00:
            dbg_print("[pci] IDE controller: ISA Compatibility mode-only controller\n");
            break;
        case 0x05:
            dbg_print("[pci] IDE controller: PCI native mode-only controller.\n");
            break;
        case 0x0a:
            dbg_print("[pci] IDE controller: ISA Compatibility mode controller.\n");
            break;
        case 0x0f:
            dbg_print("[pci] IDE controller: PCI native mode controller.\n");
            break;
        case 0x80:
            dbg_print("[pci] IDE controller: ISA Compatibility mode-only controller.\n");
            break;
        case 0x85:
            dbg_print("[pci] IDE controller: PCI native mode-only controller.\n");
            break;
        case 0x8a:
            dbg_print("[pci] IDE controller: ISA Compatibility mode controller.\n");
            break;
        case 0x8f:
            dbg_print("[pci] IDE controller: PCI native mode controller.\n");
            break;
        default:
            dbg_print("[pci] IDE controller: unknown interface.\n");
            break;
        }
        ata_probe(bus, dev, func);
        break;
    case 0x0200:        // ethernet controller
        dbg_print("[pci] ethernet controller.\n");
        break;
    case 0x0300:        // VGA compatible controller
        switch (prog) {
        case 0x00:
            dbg_print("[pci] VGA controller.\n");
            break;
        case 0x01:
            dbg_print("[pci] VGA 8514-compatible controller.\n");
            break;
        }
        break;
    case 0x0600:        // host bridge
        dbg_print("[pci] host bridge.\n");
        break;
    case 0x0601:        // ISA bridge
        dbg_print("[pci] ISA bridge.\n");
        break;
    case 0x0602:
        dbg_print("[pci] EISA bridge.\n");
        break;
    case 0x0603:
        dbg_print("[pci] MCA bridge.\n");
        break;
    case 0x0604:        // PCI-to-PCI bridge
        switch (prog) {
        case 0x00:
            dbg_print("[pci] PCI-to-PCI bridge: normal decode.\n");
            break;
        case 0x01:
            dbg_print("[pci] PCI-to-PCI bridge: subtractive decode.\n");
            break;
        default:
            dbg_print("[pci] wrong prog!\n");
            break;
        }
        dbg_print("      second bus number is %d.\n", (pci_read(bus, dev, func, 24) >> 8) & 0xff);
        // TODO: call pci_check_bus recursively
        break;
    case 0x0609:
        switch (prog) {
        case 0x40:
            dbg_print("[pci] PCI-to-PCI bridge: primary bus towards host CPU.\n");
            break;
        case 0x80:
            dbg_print("[pci] PCI-to-PCI bridge: secondary bus towards host CPU.\n");
            break;
        default:
            dbg_print("[pci] wrong prog!\n");
            break;
        }
        dbg_print("      second bus number is %d.\n", (pci_read(bus, dev, func, 24) >> 8) & 0xff);
        break;
    case 0x0680:
        dbg_print("[pci] unknown bridge.\n");
        break;
    default:
        dbg_print("[pci] unknown pci device 0x%04x, prog if %d.\n", ccode, prog);
        break;
    }

    // // TODO: base 6 sub 9 is also pci-to-pci bridge
    // if ((6 == base) && (4 == sub)) {
    //     u32 reg6 = pci_read(bus, dev, func, 24);
    //     dbg_print("second bus no.%d\n", (reg6 >> 8) & 0xff);
    // }
}

static void pci_check_dev(u8 bus, u8 dev) {
    if (0xffff == get_vendor_id(bus, dev, 0)) {
        return;
    }
    pci_check_func(bus, dev, 0);

    // if this is multi function device, check remaining functions
    if (0 != (get_header_type(bus, dev, 0) & 0x80)) {
        for (u8 f = 1; f < 8; ++f) {
            if (0xffff == get_vendor_id(bus, dev, f)) {
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

void pci_lib_init() {
    if (NULL != acpi_mcfg) {
        dbg_print("PCI Express present, we can use mapped memory.\n");
    }

    pci_check_bus(0);
}
