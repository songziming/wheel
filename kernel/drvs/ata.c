#include <wheel.h>

// fields of status register
#define STATUS_BSY  (1U << 7)
#define STATUS_DRDY (1U << 6)
#define STATUS_DF   (1U << 5)
#define STATUS_DRQ  (1U << 3)
#define STATUS_ERR  (1U << 0)

// ATA commands
#define CMD_READ_PIO        0x20
#define CMD_READ_PIO_EXT    0x24
#define CMD_READ_DMA        0xc8
#define CMD_READ_DMA_EXT    0x25
#define CMD_WRITE_PIO       0x30
#define CMD_WRITE_PIO_EXT   0x34
#define CMD_WRITE_DMA       0xca
#define CMD_WRITE_DMA_EXT   0x35
#define CMD_CACHE_FLUSH     0xe7
#define CMD_CACHE_FLUSH_EXT 0xea
#define CMD_PACKET          0xa0
#define CMD_IDENTIFY_PACKET 0xa1
#define CMD_IDENTIFY        0xec

typedef struct ata_dev {
    blk_dev_t blk;
    u16       cmd;
    u16       ctrl;
    u16       bmide;
    u32       flags;
    char      serial[21];
    char      rev   [ 9];
    char      model [41];
    int       ver;      // ata/atapi spec version number
} ata_dev_t;

// flag bits
#define ATA_SLAVE       1
#define ATA_REMOVABLE   2
#define ATA_LBA         4
#define ATA_LBA48       8
#define ATA_DMA         16

//------------------------------------------------------------------------------
// helper functions

// read alternate status 4 times
static inline void ata_wait(u16 ctrl) {
    in8(ctrl+2);
    in8(ctrl+2);
    in8(ctrl+2);
    in8(ctrl+2);
    in8(ctrl+2);
}

// reset both master and slave on the bus
static inline void ata_reset(ata_dev_t * ata) {
    out8(ata->ctrl+2, 0x04);    // set SRST
    ata_wait(ata->ctrl);
    out8(ata->ctrl+2, 0);       // clear SRST bit again
    ata_wait(ata->ctrl);
}

// read n sectors, return the number of sectors read, -1 for fail
static usize ata_read_pio(ata_dev_t * ata, u64 sector, usize n, u16 * buf) {
    u32 lba = sector & 0x0fffffffU;         // LBA-28

    u8 select = (ata->flags & ATA_SLAVE) ? 0xf0 : 0xe0;
    select |= (lba >> 24) & 0x0f;
    out8(ata->cmd+6, select);              // use lba
    ata_wait(ata->ctrl);

    out8(ata->cmd+2, n);                   // sector count
    out8(ata->cmd+3,  lba        & 0xff);  // LBA low
    out8(ata->cmd+4, (lba >>  8) & 0xff);  // LBA mid
    out8(ata->cmd+5, (lba >> 16) & 0xff);  // LBA high

    out8(ata->cmd+7, CMD_READ_PIO);        // send read command
    ata_wait(ata->ctrl);

    while (1) {
        u8 status = in8(ata->cmd+7);

        if (status & STATUS_ERR) {
            // raise exception and software reset?
            ata_reset(ata);
            return -1;
        }

        if ((0 == (status & STATUS_BSY)) && (0 != (status & STATUS_DRQ))) {
            break;
        }
    }

    // read sector content
    for (unsigned i = 0; i < 256 * n; ++i) {
        buf[i] = in16(ata->cmd+0);
    }

    return n;
}

static usize ata_write_pio(ata_dev_t * ata, u64 sector, usize n, u16 * buf) {
    u32 lba = sector & 0x0fffffffU;         // LBA-28

    u8 select = (ata->flags & ATA_SLAVE) ? 0xf0 : 0xe0;
    select |= (lba >> 24) & 0x0f;
    out8(ata->cmd+6, select);              // use lba
    ata_wait(ata->ctrl);

    out8(ata->cmd+2, n);                   // sector count
    out8(ata->cmd+3,  lba        & 0xff);  // LBA low
    out8(ata->cmd+4, (lba >>  8) & 0xff);  // LBA mid
    out8(ata->cmd+5, (lba >> 16) & 0xff);  // LBA high

    out8(ata->cmd+7, CMD_WRITE_PIO);       // send read command
    ata_wait(ata->ctrl);

    while (1) {
        u8 status = in8(ata->cmd+7);

        if (status & STATUS_ERR) {
            // raise exception and software reset?
            ata_reset(ata);
            return -1;
        }

        if ((0 == (status & STATUS_BSY)) && (0 != (status & STATUS_DRQ))) {
            break;
        }
    }

    // read sector content
    for (unsigned i = 0; i < 256 * n; ++i) {
        out16(ata->cmd+0, buf[i]);
    }

    return n;
}


//------------------------------------------------------------------------------
// send identify command

static const blk_ops_t ata_ops = {
    .read  = (blk_read_t)  ata_read_pio,
    .write = (blk_write_t) ata_write_pio,
};

static void ata_delete(ata_dev_t * dev) {
    kmem_free(sizeof(ata_dev_t), dev);
}

// return NULL if ata drive not exist
static blk_dev_t * ata_identify(u16 cmd, u16 ctrl, u16 bmide, int slave) {
    // select drive and send identify command
    out8(cmd+6, slave ? 0xb0 : 0xa0);
    ata_wait(ctrl);
    out8(cmd+7, CMD_IDENTIFY);
    ata_wait(ctrl);

    // check status register
    if (0 == in8(cmd+7)) {
        return NULL;
    }

    while (1) {
        u8 status = in8(cmd+7);

        // if ERR set, might be PACKET or SATA, check the signature
        if (0 != (status & STATUS_ERR)) {
            union {
                u8  b[4];
                u32 l;
            } u;
            u.b[0] = in8(cmd+2);    // sector count
            u.b[1] = in8(cmd+3);    // lba low
            u.b[2] = in8(cmd+4);    // lba mid
            u.b[3] = in8(cmd+5);    // lba high

            // TODO: only check lba mid/high is enough?
            // TODO: add support to SATA and SATA-PI
            switch (u.l) {
            case 0x00000101:
            default:
                // dbg_print("not ATA.\n");
                return NULL;
            case 0xeb140101:
            case 0x96690101:
                // dbg_print("ATA-PI.\n");
                // TODO: send identify-packet command and try again
                return NULL;
            case 0xc33c0101:
                // dbg_print("SATA.\n");
                return NULL;
            }
        }

        // wait until success
        if ((0 == (status & STATUS_BSY)) && (0 != (status & STATUS_DRQ))) {
            break;
        }
    }

    // read identify info
    u16 info[256];
    for (int i = 0; i < 256; ++i) {
        info[i] = in16(cmd);
    }

    // verify flags.ata bit
    if (0 != (info[0] & 0x8000)) {
        return NULL;
    }

    // we are sure this is ata drive
    ata_dev_t * ata = (ata_dev_t *) kmem_alloc(sizeof(ata_dev_t));
    blk_dev_init(&ata->blk, ata_delete, &ata_ops, BLK_READ|BLK_WRITE);
    ata->cmd   = cmd;
    ata->ctrl  = ctrl;
    ata->bmide = bmide;
    ata->flags = slave ? ATA_SLAVE : 0;

    // copy serial number and model info
    memcpy(ata->serial, &info[10], 20);
    memcpy(ata->rev,    &info[23], 8);
    memcpy(ata->model,  &info[27], 40);

    if (0 != (info[0] & 0x0080)) {
        ata->flags |= ATA_REMOVABLE;
    }
    if (0 != (info[49] & 0x0200)) {
        ata->flags |= ATA_LBA;
    }
    if (0 != (info[49] & 0x0100)) {
        ata->flags |= ATA_DMA;
    }
    if (info[83] & 0x0200) {
        ata->flags |= ATA_LBA48;
    }

    // get major revision number
    u16 ver = info[80];
    if ((0x0000 == ver) || (0xffff == ver)) {
        ata->ver = -1;
    } else {
        for (int l = 14; l >= 3; --l) {
            if (0 != (ver & (1U << l))) {
                ata->ver = l;
                break;
            }
        }
    }

    ata->blk.sec_size = 512;
    if (ata->flags & ATA_LBA48) {
        ata->blk.sec_count = * (u64 *) &info[100];  // lba-48
    } else {
        ata->blk.sec_count = * (u32 *) &info[60];   // lba-28
    }

    dbg_print("[ata] installing block device.\n");
    blk_dev_regist((blk_dev_t *) ata);
    if (YES != partprobe((blk_dev_t *) ata)) {
        volume_create((blk_dev_t *) ata, 0, ata->blk.sec_count);
    }
    return (blk_dev_t *) ata;
}

//------------------------------------------------------------------------------

void ata_probe(u8 bus, u8 dev, u8 func) {
    u32 reg0   = pci_read(bus, dev, func, 0);
    u16 vendor =  reg0        & 0xffff;
    u16 device = (reg0 >> 16) & 0xffff;
    dbg_print("[ata] probing pci %04x:%04x.\n", vendor, device);

    // read interrupt line, try changing it to 0xfe
    u32 regf = pci_read(bus, dev, func, 0x3c);
    pci_write(bus, dev, func, 0x3c, (regf & ~0xff) | 0xfe);

    // read interrupt line again
    regf = pci_read(bus, dev, func, 0x3c);
    if (0xfe == (regf & 0xff)) {
        // irq is configurable, setting it to 14
        pci_write(bus, dev, func, 0x3c, (regf & ~0xff) | 14);
    } else {
        u32 reg2 = pci_read(bus, dev, func, 8);
        u8  prog = (reg2 >> 8) & 0xff;
        if ((0x80 == prog) || (0x8a == prog)) {
            // this is parallel IDE controller, uses IRQ 14 and 15
        } else {
            // not configurable
        }
    }

    // create new device object, then regist to the kernel
    u16 ch0_cmd, ch0_ctrl, ch0_bmide;
    u16 ch1_cmd, ch1_ctrl, ch1_bmide;

    // read BAR, get command/control port of primary and secondary bus
    // if bar value is 0 or one, then use standard io port
    u32 bar0 = pci_read(bus, dev, func, 0x10);
    u32 bar1 = pci_read(bus, dev, func, 0x14);
    u32 bar2 = pci_read(bus, dev, func, 0x18);
    u32 bar3 = pci_read(bus, dev, func, 0x1c);
    u32 bar4 = pci_read(bus, dev, func, 0x20);

    if (bar0 <= 1) {
        ch0_cmd = 0x01f0;
    } else if (bar0 & 1) {
        ch0_cmd = (u16) (bar0 & ~0x03);
    } else {
        goto error;
    }
    if (bar1 <= 1) {
        ch0_ctrl = 0x03f4;
    } else if (bar1 & 1) {
        ch0_ctrl = (u16) (bar1 & ~0x03);
    } else {
        goto error;
    }
    if (bar2 <= 1) {
        ch1_cmd = 0x0170;
    } else if (bar2 & 1) {
        ch1_cmd = (u16) (bar2 & ~0x03);
    } else {
        goto error;
    }
    if (bar3 <= 1) {
        ch1_ctrl = 0x0374;
    } else if (bar3 & 1) {
        ch1_ctrl = (u16) (bar3 & ~0x03);
    } else {
        goto error;
    }
    if (bar4 & 1) {
        ch0_bmide = (u16) (bar4 & ~0x03);
        ch1_bmide = ch0_bmide + 8;
    } else {
        goto error;
    }

    // disable irq (temporarily)
    out8(ch0_ctrl+2, 2);
    out8(ch1_ctrl+2, 2);

    // identify each device
    ata_identify(ch0_cmd, ch0_ctrl, ch0_bmide, 0); // primary master
    ata_identify(ch0_cmd, ch0_ctrl, ch0_bmide, 1); // primary slave
    ata_identify(ch1_cmd, ch1_ctrl, ch1_bmide, 0); // secondary master
    ata_identify(ch1_cmd, ch1_ctrl, ch1_bmide, 1); // secondary slave
    return;

error:
    dbg_print("[ata] device init error, not using it.\n");
    return;
}
