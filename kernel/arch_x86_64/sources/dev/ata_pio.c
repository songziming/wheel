// 端口模式的硬盘驱动（俗称的 IDE）
// 此模式性能很差，但所有 ATA 硬盘都支持
// 模拟器通常不提供 AHCI 支持，只能用 PIO 操作

#include <wheel.h>
#include <pci.h>
#include <cpu/rw.h>



// 主板通常有两个 ATA 接口通道，分别称为 primary 和 secondary（还可以更多，但很少见）
// 每个通道又可以接两个设备，称为 master 和 slave，通过 device 寄存器 bit 4 区分


// control 寄存器各 bit
#define CTL_IEN     2   // 禁用该设备发送中断
#define CTL_RESET   4   // 设备重置（需保持至少 5us）

// 状态寄存器各 bit
#define STT_ERR     0x01
#define STT_DRQ     0x08    // data request
#define STT_READY   0x40
#define STT_BUSY    0x80

// 命令字节
#define ATA_IDENTIFY    0xec
#define ATAPI_IDENTIFY  0xa1





// 向硬盘发送命令的方法：将命令内容写入 command regs，写 control reg 发送


// 每次向 ATA 发送命令，都要等待 400ns，然后不断读 status，直到 BUSY bit 清零
// 读取 15 次，保留最后一次读取结果，差不多就是 400ns



// TODO 本驱动只需支持 ATA 一种
enum ata_type {
    ATA,
    ATAPI,
    SATA,
};

enum ata_flag {
    ATA_SLAVE     = 1,
    ATA_DMA       = 2,
    ATA_LBA       = 4,
    ATA_LBA48     = 8,
    ATA_REMOVABLE = 16,
};


typedef struct ata_dev {
    uint16_t cmd;   // IO base port
    uint16_t ctl;   // dev control reg / alt status
    uint8_t  type;
    uint8_t  flags;

    char    serial[20];
    char    revision[8];
    char    model[40];

    int     ver;
    int     sec_size;
    int     sec_count;
} ata_dev_t;




// TODO 使用自旋锁保护

static int g_ata_count = 0;
static ata_dev_t g_devices[4];

static int g_selected = 0; // 当前选中的设备





// 选中一个设备，接下来可以像这个设备收发命令
static ata_dev_t *select_device(int id) {
    ASSERT(id < g_ata_count);

    ata_dev_t *dev = &g_devices[id];

    if (id == g_selected) {
        return dev;
    }

    g_selected = id;

    // TODO 应该开启 LBA
    out8(dev->cmd + 6, (ATA_SLAVE & dev->flags) ? 0xb0 : 0xa0);
    for (int i = 0; i < 14; ++i) {
        in8(dev->cmd + 7);
    }
    while(0 == (in8(dev->cmd + 7) & STT_READY)) {}

    return dev;
}





// 发送 IDENTITY 命令，获取硬盘参数
// 如果设备存在则返回 1，不存在则返回 0
static int ata_identify(ata_dev_t *dev) {
    // out8(dev->cmd + 2, 0);
    // out8(dev->cmd + 3, 0);
    // out8(dev->cmd + 4, 0);
    // out8(dev->cmd + 5, 0);
    out8(dev->cmd + 7, ATA_IDENTIFY);

    // 等待 IDENTIFY 结束
    while (in8(dev->cmd + 7) & STT_BUSY) {}

    // SATA 和 ATAPI 对于 IDENTIFY 命令应该返回 ERR，并设置 command block regs
    // 但某些 ATAPI 设备不遵循标准，不报 ERR，因此直接检查 command block regs
    // 对于 ATA，执行 IDENTIFY 的结果：in8(0x1f4)=0, in8(0x1f5)=0
    // 对于 ATAPI，执行 IDENTIFY 的结果：in8(0x1f4)=0x14, in8(0x1f5)=0xeb
    // 对于 SATA，执行 IDENTIFY 的结果：in8(0x1f4)=0x3c, in8(0x1f5)=0xc3
    uint8_t r4 = in8(dev->cmd + 4);
    uint8_t r5 = in8(dev->cmd + 5);
    if ((0x14 == r4) && (0xeb == r5)) {
        dev->type = ATAPI;
        klog("this is ATA-PI!\n");
        return 0;
    } else if ((0x3c == r4) && (0xc3 == r5)) {
        dev->type = SATA;
        klog("this is SATA!\n");
        return 0;
    } else if (r4 || r5) {
        dev->type = -1;
        klog("unknown ATA device type %x-%x\n", r4, r5);
        return 0;
    }

    // 等待 data ready
    while (1) {
        uint8_t status = in8(dev->cmd + 7);
        if ((STT_DRQ & status) && !(STT_BUSY & status)) {
            break;
        }
    }

    // 读取数据，共 256 个 uint16
    uint16_t info[256];
    for (int i = 0; i < 256; ++i) {
        info[i] = in16(dev->cmd + 0);
    }

    // verify flags.ata bit
    if (0 != (info[0] & 0x8000)) {
        klog("not ATA!\n");
        return 0;
    }

    // copy serial number and model info
    memcpy(dev->serial,   &info[10], 20);
    memcpy(dev->revision, &info[23], 8);
    memcpy(dev->model,    &info[27], 40);

    if (0 != (info[0] & 0x0080)) {
        dev->flags |= ATA_REMOVABLE;
    }
    if (0 != (info[49] & 0x0200)) {
        dev->flags |= ATA_LBA;
    }
    if (0 != (info[49] & 0x0100)) {
        dev->flags |= ATA_DMA;
    }
    if (info[83] & 0x0200) {
        dev->flags |= ATA_LBA48;
    }

    // get major revision number
    uint16_t ver = info[80];
    if ((0x0000 == ver) || (0xffff == ver)) {
        dev->ver = -1;
    } else {
        for (int l = 14; l >= 3; --l) {
            if (0 != (ver & (1U << l))) {
                dev->ver = l;
                break;
            }
        }
    }

    dev->sec_size = 512;
    if (dev->flags & ATA_LBA48) {
        dev->sec_count = *(uint64_t *)&info[100];  // lba-48
    } else {
        dev->sec_count = *(uint32_t *)&info[60];   // lba-28
    }

    return 1;
}


// 读取 n 个扇区
static void ata_read(ata_dev_t *dev, uint64_t sec) {
    ASSERT(NULL != dev);

    if (dev->flags & ATA_LBA48) {
        sec &= 0x0000ffffffffffffUL; // lba-48
    } else if (dev->flags & ATA_LBA) {
        sec &= 0x0fffffffU; // lba-28
    }

    //
}








static INIT_TEXT void ata_check(uint16_t cmd, uint16_t ctl, int slave) {
    out8(cmd + 6, slave ? 0xb0 : 0xa0);
    for (int i = 0; i < 14; ++i) {
        in8(cmd + 7);
    }
    if (0 == in8(cmd + 7)) {
        return; // 设备不存在
    }

    ata_dev_t *dev = &g_devices[g_ata_count];
    dev->cmd   = cmd;
    dev->ctl   = ctl;
    dev->flags = slave ? ATA_SLAVE : 0;

    if (ata_identify(dev)) {
        ++g_ata_count;
        klog("serial %.20s, revision %.8s, model %.40s, ver=%d, sec-num %d\n",
            dev->serial, dev->revision, dev->model, dev->ver, dev->sec_count);

        // TODO 向系统注册一个块设备，设备名体现所在的通道
    }
}


// 如果 PCI 设备枚举未找到任何 AHCI 再调用这个函数
// 或者是找到了 PCI IDE 设备，却不支持 PCI native mode
INIT_TEXT void ata_probe() {
    ata_check(0x1f0, 0x3f6, 0); // primary master
    ata_check(0x1f0, 0x3f6, 1); // primary slave
    ata_check(0x170, 0x376, 0); // secondary master
    ata_check(0x170, 0x376, 1); // secondary slave
}
