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
    // ata_wait(ctrl_base);
    out8(ctrl_base, 0);     // clear SRST bit again
    // ata_wait(ctrl_base);
}

void ata_identify_packet(u16 io_base, u16 ctrl_base, int slave) {
    //
}

void ata_identify(u16 io_base, u16 ctrl_base, int slave) {
    // send identify command
    out8(io_base+6, (YES == slave) ? 0xb0 : 0xa0);
    ata_wait(ctrl_base);
    out8(io_base+7, 0xec);
    ata_wait(ctrl_base);

    // check status register
    if (0 == in8(io_base+7)) {
        dbg_print("device not exist.\n");
        return;
    }

    // if success,
    // BSY  (bit 7) should be zero
    // DRDY (bit 6) should be one
    // DF   (bit 5) should be zero
    // DRQ  (bit 3) should be zero
    // ERR  (bit 0) should be zero

    int packet = NO;
    while (1) {
        u8 status = in8(io_base+7);
        if (0 != (status & STATUS_ERR)) {
            packet = YES;
            break;
        }
        if ((0 == (status & STATUS_BSY)) && (0 != (status & STATUS_DRQ))) {
            break;
        }
    }

    if (YES == packet) {
        // this might be ATA-PI device, check the signature
        union {
            u8  b[4];
            u32 l;
        } u;
        u.b[0] = in8(io_base+2);    // sector count
        u.b[1] = in8(io_base+3);    // lba low
        u.b[2] = in8(io_base+4);    // lba mid
        u.b[3] = in8(io_base+5);    // lba high

        // TODO: maybe some drive doesn't follow the spec
        if (0x00000101 == u.l) {
            dbg_print("this device does not implement PACKET.\n");
        }
        if (0xeb140101 == u.l) {
            dbg_print("this device implement PACKET.\n");
        }
        // u8 cl = in8(io_base+4);
        // u8 ch = in8(io_base+5);
        // if ((cl == 0x14) && (ch == 0xeb)) {
        //     dbg_print("this is ATA-PI.\n");
        // } else if ((cl == 0x69) && (ch == 0x96)) {
        //     dbg_print("this is SATA-PI.\n");
        // } else {
        //     dbg_print("cannot decide %02x:%02x.\n", cl, ch);
        // }
        return;
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
        dbg_print("this is ATA device (confirmed).\n");
    }
    if (0 != (flags & 0x0080)) {
        dbg_print("this is removable device.\n");
    }

    // get serial number (20 ascii characters)
    char serial[21];
    memcpy(serial, &info[10], 20);
    serial[20] = '\0';
    dbg_print("serial number %s.\n", serial);

    // get firmware revision (8 ascii characters)
    char rev[9];
    memcpy(rev, &info[23], 8);
    rev[8] = '\0';
    dbg_print("firmware revision %s.\n", rev);

    // get model number (40 ascii characters)
    char model[41];
    memcpy(model, &info[27], 40);
    model[40] = '\0';
    dbg_print("model is %s.\n", model);

    // query capabilities
    if (0 != (info[49] & 0x0200)) {
        dbg_print("LBA is supported.\n");
    }
    if (0 != (info[49] & 0x0100)) {
        dbg_print("DMA is supported.\n");
    }

    // get major revision number
    u16 ver = info[80];
    if ((0x0000 == ver) || (0xffff == ver)) {
        dbg_print("device does not report version.\n");
    } else {
        for (int l = 3; l < 15; ++l) {
            if (0 != (ver & (1U << l))) {
                dbg_print("spec ATA/ATAPI-%d is supported.\n", l);
            }
        }
    }

    if (info[83] & 0x0200) {
        dbg_print("LBA-48 mode is supported.\n");
    }
}

void ata_init() {
    dbg_print(">>> ATA primary master.\n");
    ata_identify(0x01f0, 0x03f6, 0);

    dbg_print(">>> ATA primary slave.\n");
    ata_identify(0x01f0, 0x03f6, 1);

    dbg_print(">>> ATA secondary master.\n");
    ata_identify(0x0170, 0x0376, 0);

    dbg_print(">>> ATA secondary slave.\n");
    ata_identify(0x0170, 0x0376, 1);
}
