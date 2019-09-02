#include <wheel.h>

// ATA PIO mode driver

// ATA - AT Attachment
// IDE - Intergrated Drive Electronics

// for primary bus, io
#define PRIMARY_IO   0x01f0
#define PRIMARY_CTRL 0x03f6

// command block registers
#define CMD_DATA        (iobase + 0)
#define CMD_ERROR       (iobase + 1)
#define CMD_FEATURES    (iobase + 1)

// fields of status register
#define STATUS_BSY  (1U << 7)
#define STATUS_DRDY (1U << 6)
#define STATUS_DF   (1U << 5)
#define STATUS_DRQ  (1U << 3)
#define STATUS_ERR  (1U << 0)

typedef struct ata_controller {
    u16 io_base;
    u16 ctrl_base;
    int last_drive;     // master or slave
} ata_controller_t;

//------------------------------------------------------------------------------

extern u32  pci_read (u8 bus, u8 dev, u8 func, u8 reg);
extern void pci_write(u8 bus, u8 dev, u8 func, u8 reg, u32 data);

void ata_probe(u8 bus, u8 dev, u8 func) {
    u32 reg0 = pci_read(bus, dev, func, 0);
    u32 reg2 = pci_read(bus, dev, func, 8);
    u16 vendor =  reg0        & 0xffff;
    u16 device = (reg0 >> 16) & 0xffff;
    // u16 ccode  = (reg2 >> 16) & 0xffff;   // base and sub class code
    u8  prog   = (reg2 >>  8) & 0xff;     // programming interface

    dbg_print("found pci device %04x:%04x.\n", vendor, device);

    u32 regf = pci_read(bus, dev, func, 0x3c);
    pci_write(bus, dev, func, 0x3c, (regf & ~0xff) | 0xfe); // update int line

    if (0xfe == (pci_read(bus, dev, func, 0x3c) & 0xff)) {
        // this device needs an IRQ assignment
        dbg_print("ide needs irq assignment.\n");
    } else {
        if ((0x80 == prog) || (0x8a == prog)) {
            // this is parallel IDE controller, uses IRQ 14 and 15
            dbg_print("this is parallel ide controller.\n");
        }
    }
}


//------------------------------------------------------------------------------

// read alternate status 4 times
void ata_wait(u16 ctrl_base) {
    in8(ctrl_base);
    in8(ctrl_base);
    in8(ctrl_base);
    in8(ctrl_base);
    in8(ctrl_base);
}

// reset both master and slave on the bus
// master is selected after reset
void ata_reset(u16 ctrl_base) {
    out8(ctrl_base, 0x04);  // set SRST
    ata_wait(ctrl_base);
    out8(ctrl_base, 0);     // clear SRST bit again
    ata_wait(ctrl_base);
}

// void ata_identify_packet(u16 io_base, u16 ctrl_base, int slave) {
//     //
// }

void ata_identify(u16 io_base, u16 ctrl_base, int slave) {
    // send identify command
    out8(io_base+6, (YES == slave) ? 0xb0 : 0xa0);
    ata_wait(ctrl_base);
    out8(io_base+7, 0xec);
    ata_wait(ctrl_base);

    // check status register
    if (0 == in8(io_base+7)) {
        dbg_print("not exist.\n");
        return;
    }

    while (1) {
        u8 status = in8(io_base+7);

        // if ERR set, might be PACKET or SATA, check the signature
        if (0 != (status & STATUS_ERR)) {
            union {
                u8  b[4];
                u32 l;
            } u;
            u.b[0] = in8(io_base+2);    // sector count
            u.b[1] = in8(io_base+3);    // lba low
            u.b[2] = in8(io_base+4);    // lba mid
            u.b[3] = in8(io_base+5);    // lba high

            // TODO: add support to SATA and SATA-PI
            switch (u.l) {
            case 0x00000101:
            default:
                dbg_print("not ATA.\n");
                return;
            case 0xeb140101:
                dbg_print("ATA-PI.\n");
                return;
            case 0xc33c0101:
                dbg_print("SATA.\n");
                return;
            }
        }

        // wait until success
        if ((0 == (status & STATUS_BSY)) && (0 != (status & STATUS_DRQ))) {
            break;
        }
    }

    // read identify info
    // most of the identify info are obsolete
    u16 info[256];
    for (int i = 0; i < 256; ++i) {
        info[i] = in16(io_base);
    }

    // flags
    u16 flags = info[0];
    if (0 == (flags & 0x8000)) {
        dbg_print("ATA");
    }
    if (0 != (flags & 0x0080)) {
        dbg_print(" removable");
    }

    // // TODO: trim those strings?
    // // get serial number (20 ascii characters)
    // char serial[21];
    // memcpy(serial, &info[10], 20);
    // serial[20] = '\0';
    // dbg_print("serial number %s.\n", serial);

    // // get firmware revision (8 ascii characters)
    // char rev[9];
    // memcpy(rev, &info[23], 8);
    // rev[8] = '\0';
    // dbg_print("firmware revision %s.\n", rev);

    // // get model number (40 ascii characters)
    // char model[41];
    // memcpy(model, &info[27], 40);
    // model[40] = '\0';
    // dbg_print("model is %s.\n", model);

    // query capabilities
    if (0 != (info[49] & 0x0200)) {
        dbg_print(" LBA");
    }
    if (0 != (info[49] & 0x0100)) {
        dbg_print(" DMA");
    }

    // query command set supported
    if (info[83] & 0x0200) {
        dbg_print(" LBA-48");
    }

    // get major revision number
    u16 ver = info[80];
    if ((0x0000 == ver) || (0xffff == ver)) {
        dbg_print(" no version.\n");
    } else {
        for (int l = 14; l >= 3; --l) {
            if (0 != (ver & (1U << l))) {
                dbg_print(" ATA/ATAPI-%d.\n", l);
                break;
            }
        }
    }
}

void ata_init() {
    dbg_print("[ata] checking primary   master: ");
    ata_identify(0x01f0, 0x03f6, 0);

    dbg_print("[ata] checking primary   slave : ");
    ata_identify(0x01f0, 0x03f6, 1);

    dbg_print("[ata] checking secondary master: ");
    ata_identify(0x0170, 0x0376, 0);

    dbg_print("[ata] checking secondary slave : ");
    ata_identify(0x0170, 0x0376, 1);
}
