// ATA 硬盘控制器驱动，即俗称的 IDE
// 通过端口与 ATA 控制器通信，ATA 控制器替我们操作硬盘
// ATA 控制器有两个接口通道，分别称为 primary 和 secondary（还可以更多，但很少见）
// 每个通道又可以接两个设备，称为 master 和 slave，通过 device 寄存器 bit 4 区分


#include <wheel.h>
#include <cpu/rw.h>




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
    blk_dev_t blk;
} ata_dev_t;



// TODO 使用自旋锁保护
static blk_drv_t g_ata_driver;
// static ata_dev_t g_devices[4];
// static int g_ata_count = 0;



//------------------------------------------------------------------------------
// 检查设备是否存在
//------------------------------------------------------------------------------

// 发送 IDENTITY 命令，获取硬盘参数，设备存在则返回 1，不存在则返回 0
static INIT_TEXT int ata_identify(ata_dev_t *dev) {
    ASSERT(NULL != dev);

    // 发送 IDENTIFY 并等待执行结束
    out8(dev->cmd + 7, ATA_IDENTIFY);
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

    // 我们要求 LBA 必须支持
    if (!(ATA_LBA & dev->flags)) {
        return 0;
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

    dev->blk.sec_size = 512;
    if (dev->flags & ATA_LBA48) {
        dev->blk.sec_num = *(uint64_t *)&info[100]; // lba-48
    } else {
        dev->blk.sec_num = *(uint32_t *)&info[60];  // lba-28
    }

    return 1;
}

// 判断设备是否存在，注册块设备
static INIT_TEXT void ata_check(uint16_t cmd, uint16_t ctl, int slave) {
    out8(cmd + 6, slave ? 0xb0 : 0xa0);
    for (int i = 0; i < 14; ++i) {
        in8(cmd + 7);
    }
    if (0 == in8(cmd + 7)) {
        return; // 设备不存在
    }

    // ata_dev_t *dev = &g_devices[g_ata_count];
    ata_dev_t *dev = kernel_heap_alloc(sizeof(ata_dev_t));
    dev->cmd   = cmd;
    dev->ctl   = ctl;
    dev->flags = slave ? ATA_SLAVE : 0;

    if (!ata_identify(dev)) {
        kernel_heap_free(dev);
        return;
    }

    // 向系统注册一个块设备，设备名体现所在的通道
    dev->blk.name = strmake("ATA_%x_%s", cmd, slave ? "slave" : "master");
    add_block_device(&dev->blk, &g_ata_driver);

    // 一块硬盘可能有多个分区
    partitions_init(&dev->blk);

    klog("ata: %s, features:", dev->blk.name);
    if (ATA_REMOVABLE & dev->flags) {
        klog(" removable");
    }
    if (ATA_LBA & dev->flags) {
        klog(" lba");
    }
    if (ATA_LBA48 & dev->flags) {
        klog(" lba48");
    }
    if (ATA_DMA & dev->flags) {
        klog(" dma");
    }
    klog("\n");
}



//------------------------------------------------------------------------------
// 如果出问题，就要充值设备
//------------------------------------------------------------------------------

static void ata_reset(ata_dev_t *dev) {
    //
}



//------------------------------------------------------------------------------
// 扇区读写函数
//------------------------------------------------------------------------------

#if 0
// 记住当前选中的设备
static ata_dev_t *g_selected = NULL;

static void select_device(ata_dev_t *dev) {
    ASSERT(NULL != dev);

    if (dev == g_selected) {
        return;
    }
    g_selected = dev;

    uint8_t select = 0xa0;
    if (ATA_SLAVE & dev->flags) {
        select |= 0x10;
    }
    if ((ATA_LBA | ATA_LBA48) & dev->flags) {
        select |= 0x40;
    }
    out8(dev->cmd + 6, select);

    // 等待至少 400us
    for (int i = 0; i < 14; ++i) {
        in8(dev->cmd + 7);
    }
    while(0 == (in8(dev->cmd + 7) & STT_READY)) {}
}
#endif

static void ata_pio_read(blk_dev_t *dev, void *dst, uint64_t blk, uint32_t nblk) {
    ASSERT(NULL != dev);
    ASSERT(0 != nblk);

    ata_dev_t *ata = containerof(dev, ata_dev_t, blk);
    // select_device(ata);

    // 如果是 LBA48，选择设备、发送命令所用的字节不同
    uint8_t sel, cmd;
    if (ATA_LBA48 & ata->flags) {
        sel = 0x40;
        cmd = 0x24; // READ_SECTORS_EXT
    } else {
        sel = 0xe0 | ((blk >> 24) & 0x0f);
        cmd = 0x20; // READ_SECTORS
    }
    if (ATA_SLAVE & ata->flags) {
        sel |= 0x10;
    }

    // 选择设备
    out8(ata->cmd + 6, sel);
    for (int i = 0; i < 14; ++i) {
        in8(ata->cmd + 7);
    }
    while(0 == (in8(ata->cmd + 7) & STT_READY)) {}

    // 写入起始扇区号
    if (ata->flags & ATA_LBA48) {
        out8(ata->cmd + 2, (nblk >> 8) & 0xff);
        out8(ata->cmd + 3, (blk >> 24) & 0xff);  // LBA low
        out8(ata->cmd + 4, (blk >> 32) & 0xff);  // LBA mid
        out8(ata->cmd + 5, (blk >> 40) & 0xff);  // LBA high
    }
    out8(ata->cmd + 2, nblk & 0xff);
    out8(ata->cmd + 3,  blk        & 0xff);  // LBA low
    out8(ata->cmd + 4, (blk >>  8) & 0xff);  // LBA mid
    out8(ata->cmd + 5, (blk >> 16) & 0xff);  // LBA high

    // 发送命令
    out8(ata->cmd + 7, cmd);
    for (int i = 0; i < 14; ++i) {
        in8(ata->cmd + 7);
    }
    while (1) {
        uint8_t status = in8(ata->cmd + 7);
        if (status & STT_ERR) {
            return; // TODO 出错，重置设备
        }
        if (!(status & STT_BUSY) && (status & STT_DRQ)) {
            break;
        }
    }

    // 读取数据
    uint16_t *buff = (uint16_t *)dst;
    for (uint64_t i = 0; i < 256 * nblk; ++i) {
        buff[i] = in16(ata->cmd);
    }
}

static void ata_pio_write(blk_dev_t *dev, const void *src, uint64_t blk, uint32_t nblk) {
    ASSERT(NULL != dev);
    ASSERT(0 != nblk);

    ata_dev_t *ata = containerof(dev, ata_dev_t, blk);

    // 如果是 LBA48，选择设备、发送命令所用的字节不同
    uint8_t sel, cmd;
    if (ATA_LBA48 & ata->flags) {
        sel = 0x40;
        cmd = 0x34; // WRITE_SECTORS_EXT
    } else {
        sel = 0xe0 | ((blk >> 24) & 0x0f);
        cmd = 0x30; // WRITE_SECTORS
    }
    if (ATA_SLAVE & ata->flags) {
        sel |= 0x10;
    }

    // 选择设备
    out8(ata->cmd + 6, sel);
    for (int i = 0; i < 14; ++i) {
        in8(ata->cmd + 7);
    }
    while(0 == (in8(ata->cmd + 7) & STT_READY)) {}

    // 写入起始扇区号
    if (ata->flags & ATA_LBA48) {
        out8(ata->cmd + 2, (nblk >> 8) & 0xff);
        out8(ata->cmd + 3, (blk >> 24) & 0xff);  // LBA low
        out8(ata->cmd + 4, (blk >> 32) & 0xff);  // LBA mid
        out8(ata->cmd + 5, (blk >> 40) & 0xff);  // LBA high
    }
    out8(ata->cmd + 2, nblk & 0xff);
    out8(ata->cmd + 3,  blk        & 0xff);  // LBA low
    out8(ata->cmd + 4, (blk >>  8) & 0xff);  // LBA mid
    out8(ata->cmd + 5, (blk >> 16) & 0xff);  // LBA high

    // 发送命令
    out8(ata->cmd + 7, cmd);
    for (int i = 0; i < 14; ++i) {
        in8(ata->cmd + 7);
    }
    while (1) {
        uint8_t status = in8(ata->cmd + 7);
        if (status & STT_ERR) {
            return; // TODO 出错，重置设备
        }
        if (!(status & STT_BUSY) && (status & STT_DRQ)) {
            break;
        }
    }

    // 写入数据
    const uint16_t *buff = (const uint16_t *)src;
    for (uint64_t i = 0; i < 256 * nblk; ++i) {
        out16(ata->cmd, buff[i]); // = in16(ata->cmd);
        cpu_pause();
    }

    // 清缓存
    out16(ata->cmd + 7, 0xe7);
}



//------------------------------------------------------------------------------
// 初始化、创建设备
//------------------------------------------------------------------------------

// 注册驱动
INIT_TEXT void ata_driver_init() {
    g_ata_driver.read = ata_pio_read;
    g_ata_driver.write = ata_pio_write;
    register_block_driver(&g_ata_driver);
}

// 如果 PCI 设备枚举未找到任何 AHCI 再调用这个函数
// 或者是找到了 PCI IDE 设备，却不支持 PCI native mode
INIT_TEXT void ata_init() {
    ata_check(0x1f0, 0x3f6, 0); // primary master
    ata_check(0x1f0, 0x3f6, 1); // primary slave
    ata_check(0x170, 0x376, 0); // secondary master
    ata_check(0x170, 0x376, 1); // secondary slave
}

// 根据 PCI 设备获取控制器映射的端口号
INIT_TEXT void ata_pci_init(const pci_dev_t *dev) {
    ASSERT(NULL != dev);

    // 检查两个 channel 是否支持 PCI native 工作模式
    uint8_t progif = dev->progif;
    char ch0_native = 0;
    char ch1_native = 0;

    // 检查 primary 是否处于 PCI native mode，或者能否切换过去
    if (progif & 1) {
        klog("primary channel in PCI native mode\n");
        ch0_native = 1;
    } else if (progif & 2) {
        klog("primary channel togglable\n");
        ch0_native = 1;
        progif |= 1;
    }

    // 检查 secondary
    if (progif & 4) {
        klog("secondary channel in PCI native mode\n");
        ch1_native = 1;
    } else if (progif & 8) {
        klog("secondary channel togglable\n");
        ch1_native = 1;
        progif |= 4;
    }

    // 尝试切换到 PCI native mode
    if (progif != dev->progif) {
        uint32_t reg2 = g_pci_read(dev->bus, dev->slot, dev->func, 8);
        reg2 &= ~0xff00;
        reg2 |= (uint32_t)progif << 8;
        g_pci_write(dev->bus, dev->slot, dev->func, 8, reg2);
    }

    // 读取五个 BAR，获取各个 ATA 设备使用的端口
    // 如果某个 BAR 为零，说明这个设备使用默认端口

    // BAR0、BAR1 描述 channel0
    uint16_t ch0_cmd = 0x1f0;
    uint16_t ch0_ctl = 0x3f6;
    if (ch0_native) {
        uint32_t bar0 = g_pci_read(dev->bus, dev->slot, dev->func, 0x10);
        uint32_t bar1 = g_pci_read(dev->bus, dev->slot, dev->func, 0x14);

        if ((bar0 & 1) && (bar0 & ~3)) {
            ch0_cmd = (uint16_t)(bar0 & ~3);
        }
        if ((bar1 & 1) && (bar1 & ~3)) {
            ch0_ctl = (uint16_t)(bar1 & ~3);
        }

        klog("ATA primary native, cmd=%x, ctl=%x\n", ch0_cmd, ch0_ctl);
    }

    // BAR2、BAR3 描述 channel1
    uint16_t ch1_cmd = 0x170;
    uint16_t ch1_ctl = 0x376;
    if (ch1_native) {
        uint32_t bar2 = g_pci_read(dev->bus, dev->slot, dev->func, 0x18);
        uint32_t bar3 = g_pci_read(dev->bus, dev->slot, dev->func, 0x1c);

        if ((bar2 & 1) && (bar2 & ~3)) {
            ch1_cmd = (uint16_t)(bar2 & ~3);
        }
        if ((bar3 & 1) && (bar3 & ~3)) {
            ch1_ctl = (uint16_t)(bar3 & ~3);
        }

        klog("ATA secondary native, cmd=0x%x, ctl=%x\n", ch1_cmd, ch1_ctl);
    }

    // BAR4 描述 bus master port，是 DMA 寄存器映射的端口
    uint16_t ch0_bm = 0;
    uint16_t ch1_bm = 0;
    if (progif & (1U << 7)) {
        uint32_t bar4 = g_pci_read(dev->bus, dev->slot, dev->func, 0x20);
        if (bar4 & 1) {
            ch0_bm = (uint16_t)(bar4 & ~3);
            ch1_bm = ch0_bm + 8;
        } else {
            progif &= ~(1U << 7);
        }
        // klog("bus master IDE base 0x%x, 0x%x\n", ch0_bm, ch1_bm);
    }

    // 只要有一个 channel 处于 PCI native mode
    // 就应该读取 interrupt line，获取该设备的 IRQ
    // 两个 channel 共享同一个 IRQ
    if (ch0_native || ch1_native) {
        uint32_t regf = g_pci_read(dev->bus, dev->slot, dev->func, 0x3c);
        klog("PIC IRQ number %d", regf & 0xff);
    }

    // 使用提取的信息初始化控制器
    ata_init();
}
