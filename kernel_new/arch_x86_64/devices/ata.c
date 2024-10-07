#include "ata.h"
#include <arch_intf.h>

// #include <common.h>
#include <library/debug.h>
#include <library/format.h>
#include <library/string.h>
#include <services/block.h>
#include <services/pci.h>
#include <memory/heap.h>

#include <cpu/rw.h>


enum ata_flag {
    ATA_SECONDARY = 1,
    ATA_SLAVE     = 2,
    ATA_DMA       = 4,
    ATA_LBA       = 8,
    ATA_LBA48     = 16,
    ATA_REMOVABLE = 32,
};


// 一个 channel 可以关联两个设备：master、slave
typedef struct ata_channel {
    uint16_t io_base;
    uint16_t control_base;
    uint8_t irq;

    // uint8_t last_select; // 上一次发送的 drive select 字节
} ata_channel_t;


typedef struct ata_device {
    blk_dev_t   blk;
    uint8_t     flags;  // master 还是 slave、是否支持 LBA48、DMA 等
    // uint8_t     type;

    char        serial[20];
    char        revision[8];
    char        model[40];
    uint8_t     ver;
} ata_device_t;


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


static blk_drv_t g_ata_driver;
static ata_channel_t g_channels[2];


char *strmake(const char *fmt, ...) {
    char buf[1024];

    va_list va;
    va_start(va, fmt);
    size_t len = vsnprintk(buf, sizeof(buf) - 1, fmt, va);
    va_end(va);

    char *str = kernel_heap_alloc(len + 1);
    if (NULL == str) {
        return NULL;
    }

    memcpy(str, buf, len + 1);
    return str;
}


//------------------------------------------------------------------------------
// 读写 ATA 寄存器
//------------------------------------------------------------------------------

// 等待 busy、data-ready 全部清空
static inline int ata_wait(ata_channel_t *ch) {
    for (int i = 0; i < 1000; ++i) {
        uint8_t status = in8(ch->control_base);
        if (STT_ERR & status) {
            return 1;
        }
        if (!((STT_BUSY | STT_DRQ) & status)) {
            return 0;
        }
    }
    return 1;
}


// 等待 busy 清零，data-ready 开启
static inline int ata_wait_data(ata_channel_t *ch) {
    for (int i = 0; i < 1000; ++i) {
        uint8_t status = in8(ch->control_base);
        if (STT_ERR & status) {
            return 1;
        }
        if ((0 == (STT_BUSY & status)) && (STT_DRQ & status)) {
            return 0;
        }
    }
    return 0;
}

// 选择设备之后，至少要等 400ns
// 缓存上一次选中的设备，如果与本次的相同，则无需再次选择
static void ata_select_drive(ata_channel_t *ch, uint8_t sel) {
    // if (sel == ch->last_select) {
    //     return;
    // }
    // ch->last_select = sel;

    ata_wait(ch);
    out8(ch->io_base + 6, sel);
    for (int i = 0; i < 14; ++i) {
        in8(ch->control_base);
    }
}

// 同时重置了这个通道的 master 和 slave
static inline void ata_reset(ata_channel_t *ch) {
    out8(ch->control_base, 4); // software reset
    in8(ch->control_base);
    out8(ch->control_base, 0);
}


//------------------------------------------------------------------------------
// identify，检查设备是否存在
//------------------------------------------------------------------------------

static INIT_TEXT void ata_detect(int secondary, int slave) {
    ata_channel_t *ch = &g_channels[secondary ? 1 : 0];
    uint8_t sel = slave ? 0xb0 : 0xa0;

    const char *name_channel = secondary ? "secondary" : "primary";
    const char *name_device  = slave ? "slave" : "master";

    // log("%s-%s io-base=%x, control-base=%x sel=%x\n",
    //     secondary ? "secondary" : "primary", slave ? "slave" : "master",
    //     ch->io_base, ch->control_base, sel);

    ata_select_drive(ch, sel);
    ata_wait(ch);
    if (0 == in8(ch->io_base + 7)) {
        log("%s-%s not exist!\n", name_channel, name_device);
        return;
    }

    // 这几个寄存器必须为零
    out8(ch->io_base + 2, 0);
    out8(ch->io_base + 3, 0);
    out8(ch->io_base + 4, 0);
    out8(ch->io_base + 5, 0);

    // 发送 IDENTIFY 并等待执行结束
    out8(ch->io_base + 7, ATA_IDENTIFY);
    while (STT_BUSY & in8(ch->control_base)) {}

    // SATA 和 ATAPI 对于 IDENTIFY 命令应该返回 ERR，并设置 command block regs
    // 但某些 ATAPI 设备不遵循标准，不报 ERR，因此直接检查 command block regs
    // 对于 ATA，执行 IDENTIFY 的结果：in8(0x1f4)=0, in8(0x1f5)=0
    // 对于 ATAPI，执行 IDENTIFY 的结果：in8(0x1f4)=0x14, in8(0x1f5)=0xeb
    // 对于 SATA，执行 IDENTIFY 的结果：in8(0x1f4)=0x3c, in8(0x1f5)=0xc3
    uint8_t r4 = in8(ch->io_base + 4);
    uint8_t r5 = in8(ch->io_base + 5);
    if ((0x14 == r4) && (0xeb == r5)) {
        log("%s-%s is ATA-PI!\n", name_channel, name_device);
        return;
    } else if ((0x3c == r4) && (0xc3 == r5)) {
        log("%s-%s is SATA!\n", name_channel, name_device);
        return;
    } else if (r4 || r5) {
        log("%s-%s unknown ATA device type %x-%x\n", name_channel, name_device, r4, r5);
        return;
    }

    // 等待 data ready
    while (1) {
        uint8_t status = in8(ch->control_base);
        if ((STT_DRQ & status) && !(STT_BUSY & status)) {
            break;
        }
    }

    // 读取数据，共 256 个 uint16
    uint16_t info[256];
    for (int i = 0; i < 256; ++i) {
        info[i] = in16(ch->io_base + 0);
    }

    // verify flags.ata bit
    if (info[0] & 0x8000) {
        log("%s-%s not ATA!\n", name_channel, name_device);
        return;
    }

    // 我们要求 LBA 必须支持
    if (!(info[49] & 0x0200)) {
        log("%s-%s no LBA support!\n", name_channel, name_device);
        return;
    }

    // 确认设备存在，创建设备对象
    ata_device_t *ata = kernel_heap_alloc(sizeof(ata_device_t));
    ata->flags  = 0;
    ata->flags |= secondary ? ATA_SECONDARY : 0;
    ata->flags |= slave     ? ATA_SLAVE     : 0;
    ata->flags |= (info[0]  & 0x0080) ? ATA_REMOVABLE : 0;
    ata->flags |= (info[49] & 0x0100) ? ATA_DMA       : 0;
    ata->flags |= (info[49] & 0x0200) ? ATA_LBA       : 0;
    ata->flags |= (info[83] & 0x0200) ? ATA_LBA48     : 0;

    memcpy(ata->serial,   &info[10], 20);
    memcpy(ata->revision, &info[23], 8);
    memcpy(ata->model,    &info[27], 40);

    // get major revision number
    uint16_t ver = info[80];
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
        ata->blk.sec_num = *(uint64_t *)&info[100]; // lba-48
    } else {
        ata->blk.sec_num = *(uint32_t *)&info[60];  // lba-28
    }

    // 像系统注册这个块设备
    ata->blk.name = strmake("ata%d", secondary * 2 + slave + 1);
    add_block_device(&ata->blk, &g_ata_driver);

    // // 一块硬盘可能有多个分区
    // partitions_init(&ata->blk);
}



//------------------------------------------------------------------------------
// PIO 读写
//------------------------------------------------------------------------------

void ata_pio_read_sector(blk_dev_t *blk, void *dst, uint64_t sector, uint64_t num) {
    ata_device_t *ata = containerof(blk, ata_device_t, blk);
    ata_channel_t *ch = &g_channels[(ata->flags & ATA_SECONDARY) ? 1 : 0];

    // TODO 锁住 ata channel

    uint8_t sel = (ata->flags & ATA_SLAVE) ? 0xf0 : 0xe0; // 开启 LBA
    uint8_t cmd;

    if (ATA_LBA48 & ata->flags) {
        cmd = 0x24; // READ_SECTORS_EXT
    } else {
        sel |= (sector >> 24) & 0x0f; // LBA28，最高 4-bit 放在这里
        cmd = 0x20; // READ_SECTORS
    }

    // 选择设备
    ata_select_drive(ch, sel);
    ata_wait(ch);

    // 写入起始扇区号
    if (ata->flags & ATA_LBA48) {
        out8(ch->io_base + 2, (num >> 8) & 0xff);
        out8(ch->io_base + 3, (sector >> 24) & 0xff);  // LBA low
        out8(ch->io_base + 4, (sector >> 32) & 0xff);  // LBA mid
        out8(ch->io_base + 5, (sector >> 40) & 0xff);  // LBA high
    }
    out8(ch->io_base + 2, num & 0xff);
    out8(ch->io_base + 3,  sector        & 0xff);  // LBA low
    out8(ch->io_base + 4, (sector >>  8) & 0xff);  // LBA mid
    out8(ch->io_base + 5, (sector >> 16) & 0xff);  // LBA high

    // 发送命令
    out8(ch->io_base + 7, cmd);

    // 如果出错，需要重置设备
    if (ata_wait_data(ch)) {
        log("%s error when reading sector\n", ata->blk.name);
        ata_reset(ch);
        return;
    }

    // 读取数据
    uint16_t *buff = (uint16_t *)dst;
    for (uint64_t i = 0; i < 256 * num; ++i) {
        buff[i] = in16(ch->io_base);
    }
}

void ata_pio_write_sector(blk_dev_t *blk, const void *src, uint64_t sector, uint64_t num) {
    ata_device_t *ata = containerof(blk, ata_device_t, blk);
    ata_channel_t *ch = &g_channels[(ata->flags & ATA_SECONDARY) ? 1 : 0];

    uint8_t sel = (ata->flags & ATA_SLAVE) ? 0xf0 : 0xe0; // 开启 LBA
    uint8_t cmd;

    if (ATA_LBA48 & ata->flags) {
        cmd = 0x34; // WRITE_SECTORS_EXT
    } else {
        sel |= (sector >> 24) & 0x0f;
        cmd = 0x30; // WRITE_SECTORS
    }

    ata_select_drive(ch, sel);
    ata_wait(ch);

    // 写入起始扇区号
    if (ata->flags & ATA_LBA48) {
        out8(ch->io_base + 2, (num >> 8) & 0xff);
        out8(ch->io_base + 3, (sector >> 24) & 0xff);  // LBA low
        out8(ch->io_base + 4, (sector >> 32) & 0xff);  // LBA mid
        out8(ch->io_base + 5, (sector >> 40) & 0xff);  // LBA high
    }
    out8(ch->io_base + 2, num & 0xff);
    out8(ch->io_base + 3,  sector        & 0xff);  // LBA low
    out8(ch->io_base + 4, (sector >>  8) & 0xff);  // LBA mid
    out8(ch->io_base + 5, (sector >> 16) & 0xff);  // LBA high

    // 发送命令
    out8(ch->io_base + 7, cmd);

    // 如果出错，需要重置设备
    if (ata_wait_data(ch)) {
        log("%s error when writing sector\n", ata->blk.name);
        ata_reset(ch);
        return;
    }

    // 写入数据
    const uint16_t *buff = (const uint16_t *)src;
    for (uint64_t i = 0; i < 256 * num; ++i) {
        out16(ch->io_base, buff[i]);
        cpu_pause();
    }

    // 清缓存
    out16(ch->io_base + 7, 0xe7);
}



//------------------------------------------------------------------------------
// 注册设备驱动
//------------------------------------------------------------------------------

INIT_TEXT void ata_driver_init() {
    memset(&g_ata_driver, 0, sizeof(g_ata_driver));
    // g_ata_driver.show  = ata_show_info;
    g_ata_driver.read  = ata_pio_read_sector;
    g_ata_driver.write = ata_pio_write_sector;
    register_block_driver(&g_ata_driver);
}



//------------------------------------------------------------------------------
// PCI IDE Controller
//------------------------------------------------------------------------------

// 这部分和 PCI 相关

// 有两种工作模式：
//  1. compatibility mode，使用的端口、IRQ 固定
//  2. PCI-native mode，端口号通过 BAR 指定

// classcode=1 subclass=1
// 在初始化流程中调用
INIT_TEXT void ata_pci_lib_init(const pci_dev_t *dev) {
    ASSERT(NULL != dev);

    // 开启 IO Enable bit
    uint32_t reg1 = g_pci_read(dev->bus, dev->slot, dev->func, 4);
    if (0 == (reg1 & 1)) {
        g_pci_write(dev->bus, dev->slot, dev->func, 4, reg1 | 1);
    }

    // 检查两个 channel 是否支持 PCI native 工作模式
    uint8_t progif = dev->progif;
    char ch0_native = 0;
    char ch1_native = 0;

    // 检查 primary 是否处于 PCI native mode，或者能否切换过去
    if (progif & 1) {
        log("primary channel in PCI-native mode\n");
        ch0_native = 1;
    } else if (progif & 2) {
        log("primary channel supports PCI-native mode\n");
        ch0_native = 1;
        progif |= 1;
    }

    // 检查 secondary
    if (progif & 4) {
        log("secondary channel in PCI-native mode\n");
        ch1_native = 1;
    } else if (progif & 8) {
        log("secondary channel supports PCI-native mode\n");
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

    // 这是默认端口
    g_channels[0].io_base      = 0x1f0;
    g_channels[0].control_base = 0x3f6;
    g_channels[1].io_base      = 0x170;
    g_channels[1].control_base = 0x376;

    // 读取五个 BAR，获取各个 ATA 设备使用的端口
    // 如果某个 BAR 为零，说明这个设备使用默认端口

    // BAR0、BAR1 描述 channel0
    if (ch0_native) {
        uint32_t bar0 = g_pci_read(dev->bus, dev->slot, dev->func, 0x10);
        uint32_t bar1 = g_pci_read(dev->bus, dev->slot, dev->func, 0x14);
        if ((bar0 & 1) && (bar0 & ~3)) {
            g_channels[0].io_base = (uint16_t)(bar0 & ~3);
        }
        if ((bar1 & 1) && (bar1 & ~3)) {
            g_channels[0].control_base = (uint16_t)(bar1 & ~3);
        }
    }

    // BAR2、BAR3 描述 channel1
    if (ch1_native) {
        uint32_t bar2 = g_pci_read(dev->bus, dev->slot, dev->func, 0x18);
        uint32_t bar3 = g_pci_read(dev->bus, dev->slot, dev->func, 0x1c);
        if ((bar2 & 1) && (bar2 & ~3)) {
            g_channels[1].io_base = (uint16_t)(bar2 & ~3);
        }
        if ((bar3 & 1) && (bar3 & ~3)) {
            g_channels[1].control_base = (uint16_t)(bar3 & ~3);
        }
    }

#if 0
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
        // log("bus master IDE base 0x%x, 0x%x\n", ch0_bm, ch1_bm);
    }
#endif

    // 只要有一个 channel 处于 PCI native mode
    // 就应该读取 interrupt line，获取该设备的 IRQ
    // 两个 channel 共享同一个 IRQ
    if (ch0_native || ch1_native) {
        uint32_t regf = g_pci_read(dev->bus, dev->slot, dev->func, 0x3c);
        log("PIC IRQ number %d", regf & 0xff);
    }

    // 根据查出来的端口和 IRQ 初始化设备
    ata_detect(0, 0); // primary master
    ata_detect(0, 1); // primary slave
    ata_detect(1, 0); // secondary master
    ata_detect(1, 1); // secondary slave
}
