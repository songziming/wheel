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

typedef struct ata_channel {
    u16 cmd;
    u16 ctrl;
    // int selected;   // 0 for master, 1 for slave
    u16 bmide;
} ata_channel_t;

typedef struct ata_controller {
    ata_channel_t chan[2];          // 0 for
    u16           bus_master_ide; // controls dma
} ata_controller_t;

//------------------------------------------------------------------------------
// generic block device interface

typedef struct blk_ops blk_ops_t;
typedef struct blk_dev blk_dev_t;

typedef usize (* blk_read_t)  (blk_dev_t * blk, usize sec, usize n,       u8 * buf);
typedef usize (* blk_write_t) (blk_dev_t * blk, usize sec, usize n, const u8 * buf);

struct blk_ops {
    blk_read_t  read;
    blk_write_t write;
};

struct blk_dev {
    kref_t      ref;
    blk_ops_t * ops;
    usize       sec_size;
    usize       sec_count;
};



typedef struct ata_dev {
    blk_dev_t blk;
    u32       flags;
    usize     ata;      // channel and slave
} ata_dev_t;

// flag bits
#define ATA_LBA48   1

#define ATA_CHAN(dev)  ((ata_channel_t *) (dev->ata & ~1))
#define ATA_SLAVE(dev) ((int)             (dev->ata &  1))

//------------------------------------------------------------------------------
// helper functions

// read alternate status 4 times
static inline void ata_wait(ata_channel_t * chan) {
    in8(chan->ctrl+2);
    in8(chan->ctrl+2);
    in8(chan->ctrl+2);
    in8(chan->ctrl+2);
    in8(chan->ctrl+2);
}

// reset both master and slave on the bus
static inline void ata_reset(ata_channel_t * chan) {
    out8(chan->ctrl+2, 0x04);    // set SRST
    ata_wait(chan);
    out8(chan->ctrl+2, 0);       // clear SRST bit again
    ata_wait(chan);
}

//------------------------------------------------------------------------------
// send identify command

static usize ata_read_pio(ata_channel_t * chan, int slave, u64 sector, u16 * buf, usize n);

static void ata_identify(ata_channel_t * chan, int slave) {
    // select drive
    out8(chan->cmd+6, slave ? 0xb0 : 0xa0);
    ata_wait(chan);

    // send identify command
    out8(chan->cmd+7, CMD_IDENTIFY);
    ata_wait(chan);

    // check status register
    if (0 == in8(chan->cmd+7)) {
        // dbg_print("not exist.\n");
        return;
    }

    while (1) {
        u8 status = in8(chan->cmd+7);

        // if ERR set, might be PACKET or SATA, check the signature
        if (0 != (status & STATUS_ERR)) {
            union {
                u8  b[4];
                u32 l;
            } u;
            u.b[0] = in8(chan->cmd+2);    // sector count
            u.b[1] = in8(chan->cmd+3);    // lba low
            u.b[2] = in8(chan->cmd+4);    // lba mid
            u.b[3] = in8(chan->cmd+5);    // lba high

            // TODO: only check lba mid/high is enough?
            // TODO: add support to SATA and SATA-PI
            switch (u.l) {
            case 0x00000101:
            default:
                // dbg_print("not ATA.\n");
                return;
            case 0xeb140101:
            case 0x96690101:
                // dbg_print("ATA-PI.\n");
                // TODO: send identify-packet command and try again
                return;
            case 0xc33c0101:
                // dbg_print("SATA.\n");
                return;
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
        info[i] = in16(chan->cmd);
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

    // retrieve device parameters from identify info
    // create blk_dev instance and regist to the system

    // create block device object
    blk_dev_t * blk = kmem_alloc(sizeof(blk_dev_t));

    // most ata have 512 byte sector
    blk->sec_size  = 512;

    // get number of addressable sectors
    if (info[83] & 0x0200) {
        blk->sec_count = * (u64 *) &info[100];  // lba-48
    } else {
        blk->sec_count = * (u32 *) &info[60];   // lba-28
    }

    // blk_dev_regist(blk);
    dbg_print("reading sector 2...\n");
    u32 sec[512/4];
    ata_read_pio(chan, slave, 2, (u16 *) sec, 1);
    for (int i = 0; i < 128; ++i) {
        dbg_print(" %08x", sec[i]);
        if ((i+1) % 8 == 0) {
            dbg_print("\n");
        }
    }

    return;
}

// read n sectors, return the number of sectors read, -1 for fail
static usize ata_read_pio(ata_channel_t * chan, int slave, u64 sector, u16 * buf, usize n) {
    u32 lba = sector & 0x0fffffffU;         // LBA-28

    u8 select = 0xe0;
    select |= (slave & 1) << 4;
    select |= (lba >> 24) & 0x0f;
    out8(chan->cmd+6, select);              // use lba
    ata_wait(chan);

    out8(chan->cmd+2, n);                   // sector count
    out8(chan->cmd+3,  lba        & 0xff);  // LBA low
    out8(chan->cmd+4, (lba >>  8) & 0xff);  // LBA mid
    out8(chan->cmd+5, (lba >> 16) & 0xff);  // LBA high

    out8(chan->cmd+7, CMD_READ_PIO);        // send read command
    ata_wait(chan);

    while (1) {
        u8 status = in8(chan->cmd+7);

        if (status & STATUS_ERR) {
            // raise exception and software reset?
            ata_reset(chan);
            return -1;
        }

        if ((0 == (status & STATUS_BSY)) && (0 != (status & STATUS_DRQ))) {
            break;
        }
    }

    // read sector content
    for (unsigned i = 0; i < 256 * n; ++i) {
        buf[i] = in16(chan->cmd+0);
    }

    return n;
}

// static usize ata_write_pio(ata_channel_t * chan, int slave, u64 sector, usize n, u16 * buf) {
//     //
// }

//------------------------------------------------------------------------------

void ata_probe(u8 bus, u8 dev, u8 func) {
    u32 reg0   = pci_read(bus, dev, func, 0);
    u16 vendor =  reg0        & 0xffff;
    u16 device = (reg0 >> 16) & 0xffff;
    dbg_print("[ata] located at pci %04x:%04x.\n", vendor, device);

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
        u8  prog = (reg2 >>  8) & 0xff;
        if ((0x80 == prog) || (0x8a == prog)) {
            // this is parallel IDE controller, uses IRQ 14 and 15
        } else {
            // not configurable
        }
    }

    // create new device object, then regist to the kernel
    ata_controller_t * ata = kmem_alloc(sizeof(ata_controller_t));

    // read BAR, get command/control port of primary and secondary bus
    // if bar value is 0 or one, then use standard io port
    u32 bar0 = pci_read(bus, dev, func, 0x10);
    u32 bar1 = pci_read(bus, dev, func, 0x14);
    u32 bar2 = pci_read(bus, dev, func, 0x18);
    u32 bar3 = pci_read(bus, dev, func, 0x1c);
    u32 bar4 = pci_read(bus, dev, func, 0x20);

    if (bar0 <= 1) {
        ata->chan[0].cmd = 0x01f0;
    } else if (bar0 & 1) {
        ata->chan[0].cmd = (u16) (bar0 & ~0x03);
    } else {
        goto error;
    }

    if (bar1 <= 1) {
        ata->chan[0].ctrl = 0x03f4;
    } else if (bar1 & 1) {
        ata->chan[0].ctrl = (u16) (bar1 & ~0x03);
    } else {
        goto error;
    }

    if (bar2 <= 1) {
        ata->chan[1].cmd = 0x0170;
    } else if (bar2 & 1) {
        ata->chan[1].cmd = (u16) (bar2 & ~0x03);
    } else {
        goto error;
    }

    if (bar3 <= 1) {
        ata->chan[1].ctrl = 0x0374;
    } else if (bar3 & 1) {
        ata->chan[1].ctrl = (u16) (bar3 & ~0x03);
    } else {
        goto error;
    }

    if (bar4 & 1) {
        ata->bus_master_ide = (u16) (bar4 & ~0x03);
    } else {
        goto error;
    }

    // for driver, they belongs to the same controller
    // master and slave share the same channel
    // so the driver function needs to implement mutual exclusion

    // but for the system, they are completely difference devices
    // each has their own blk_dev object registered

    // disable irq (temporarily)
    out8(ata->chan[0].ctrl+2, 2);
    out8(ata->chan[1].ctrl+2, 2);

    // identify each device
    ata_identify(&ata->chan[0], 0); // primary master
    ata_identify(&ata->chan[0], 1); // primary slave
    ata_identify(&ata->chan[1], 0); // secondary master
    ata_identify(&ata->chan[1], 1); // secondary slave

error:
    dbg_print("[ata] device init error, not using it.\n");
    kmem_free(sizeof(ata_controller_t), ata);
    return;
}
