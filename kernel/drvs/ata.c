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

typedef struct ata_controller {
    u16 io_base;
    u16 ctrl_base;
    int last_drive;     // master or slave
} ata_controller_t;

// void select_drive(int drive) {
//     //
// }

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

void ata_init2(u16 io_base, u16 ctrl_base, int slave) {
    // select drive
    out8(io_base+6, 0xa0 | ((slave << 4) & 0x10));
    tick_delay(1);

    // send identify command
    out8(io_base+7, 0xec);
    tick_delay(1);

    if (0 == in8(io_base+7)) {
        dbg_print("device not exist.\n");
        return;
    }

    int err = 0;
    while (1) {
        u8 status = in8(io_base+7);
        if (0 != (status & 0x01)) {
            dbg_print("error, device not ATA.\n");
            err = 1;
            break;
        }
        if ((0 == (status & 0x80)) && (0 != (status & 0x08))) {
            break;
        }
    }

    if (err) {
        u8 cl = in8(io_base+4);
        u8 ch = in8(io_base+5);
        if ((cl == 0x14) && (ch == 0xeb)) {
            dbg_print("this is ATA-PI.\n");
        } else if ((cl == 0x69) && (ch == 0x96)) {
            dbg_print("this is SATA-PI.\n");
        } else {
            dbg_print("cannot decide %02x:%02x.\n", cl, ch);
            return;
        }
    }

    // read identify info
    u16 info[256];
    for (int i = 0; i < 256; ++i) {
        info[i] = in16(io_base);
    }

    char serial[21];
    memcpy(serial, &info[10], 20);
    serial[20] = '\0';
    dbg_print("serial number %s.\n", serial);

    char model[41];
    memcpy(model, &info[27], 40);
    model[40] = '\0';
    dbg_print("model is %s.\n", model);

    if (info[83] & 0x0200) {
        dbg_print("LBA-48 mode is supported.\n");
    }
}

void ata_init() {
    dbg_print(">>> ATA primary master.\n");
    ata_init2(0x01f0, 0x03f6, 0);

    dbg_print(">>> ATA primary slave.\n");
    ata_init2(0x01f0, 0x03f6, 1);

    dbg_print(">>> ATA secondary master.\n");
    ata_init2(0x0170, 0x0376, 0);

    dbg_print(">>> ATA secondary slave.\n");
    ata_init2(0x0170, 0x0376, 1);
}
