#include "ata_pio.h"
#include <arch_api.h>
#include <mutex.h>
#include <kstring.h>
#include <debug.h>
#include <block.h>
#include <heap.h>
#include <partition.h>



// PIO 是 ATA 最慢的访问方式，但驱动最简单
// 这个模块无需追求高性能

// ATA 有两种工作模式：
// compatibility mode，端口地址固定（1984 PC AT 之后从未改变），虚拟机使用这个
// native mode，需要解析 PCI，读取 BAR 获得真正的端口号


// 默认端口
#define ATA_PRIMARY_IO      0x1f0
#define ATA_PRIMARY_CTL     0x3f6
#define ATA_SECONDARY_IO    0x170
#define ATA_SECONDARY_CTL   0x376

// 端口列表：
//  - IO + 0    : data
//  - IO + 1    : R)error, W)features
//  - IO + 2    : sector count
//  - IO + 3    : LBA low
//  - IO + 4    : LBA mid
//  - IO + 5    : LBA high
//  - IO + 7    : R)status, W)command
//  - CTL + 0   : R)alt_status, W)dev_control
//  - CTL + 1   : drive address

// control 寄存器各 bit
#define CTL_IEN     2   // 禁用该设备发送中断
#define CTL_RESET   4   // 设备重置（需保持至少 5us）

// 状态寄存器（IO+7 R）各 bit
#define STT_ERR     0x01
#define STT_DRQ     0x08    // data request
#define STT_READY   0x40
#define STT_BUSY    0x80

// 命令字节
#define ATA_IDENTIFY    0xec
#define ATAPI_IDENTIFY  0xa1



// 一个 channel 可以关联两个设备：master、slave
typedef struct ata_channel {
    mutex_t  mutex;
    uint16_t io_base;       // io 端口
    uint16_t control_base;  // ctrl 端口
    uint8_t  irq;
} ata_channel_t;

// 两个通道，每个通道可以关联两个设备
static ata_channel_t g_channels[2];



//------------------------------------------------------------------------------
// ATA 块设备定义
//------------------------------------------------------------------------------

enum ata_flag {
    ATA_SECONDARY = 1,
    ATA_SLAVE     = 2,
    ATA_DMA       = 4,
    ATA_LBA       = 8,
    ATA_LBA48     = 16,
    ATA_REMOVABLE = 32,
};

typedef struct ata_device {
    block_dev_t blk;
    uint8_t     flags;  // master 还是 slave、是否支持 LBA48、DMA 等
    char        serial[20];
    char        revision[8];
    char        model[40];
    uint8_t     ver;
} ata_device_t;


static void ata_pio_read_sector(block_dev_t *blk, void *dst, uint64_t sector, uint64_t num);
static void ata_pio_write_sector(block_dev_t *blk, const void *src, uint64_t sector, uint64_t num);
static const block_ops_t ata_ops = {
    .read = ata_pio_read_sector,
    .write = ata_pio_write_sector,
};

static ata_device_t ata_devs[4];


//------------------------------------------------------------------------------
// 读写 ATA 寄存器
//------------------------------------------------------------------------------

// 读取 alternate_status 寄存器，等待 BSY、DRQ 清空，之后才可以发送命令
static inline int ata_wait(ata_channel_t *ch) {
    for (int i = 0; i < 1000; ++i) {
        uint8_t status = in8(ch->control_base);
        if (STT_ERR & status) {
            return 1;
        }
        if (!((STT_BUSY | STT_DRQ) & status)) {
            return 0;
        }
        cpu_pause();
    }
    return 1;
}

// 选择设备之后，至少要等 400ns，之后读出的 status 才是正确的
// 读取 alternate_status 14 次，丢弃它们的结果
// 第 15 次读出的 alternate_status 才是有效的
// TODO 缓存上一次选中的设备，如果与本次的相同，则无需再次选择
static void ata_select_drive(ata_channel_t *ch, uint8_t sel) {
    ata_wait(ch);
    out8(ch->io_base + 6, sel);
    for (int i = 0; i < 14; ++i) {
        (void)in8(ch->control_base);
    }
}

// 读取 alternate_status，等待 BSY 清空、DRQ 置位，之后才可以读数据
// 准备数据需要很长时间
static inline int ata_wait_data(ata_channel_t *ch) {
    while (1) {
        uint8_t status = in8(ch->control_base);
        if (STT_ERR & status) {
            return 1;
        }
        if ((0 == (STT_BUSY & status)) && (STT_DRQ & status)) {
            return 0;
        }
        cpu_pause();
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

static INIT_TEXT void ata_detect(ata_device_t *ata, int secondary, int slave) {
    ata_channel_t *ch = &g_channels[secondary ? 1 : 0];
    uint8_t sel = slave ? 0xb0 : 0xa0;

    const char *chn = secondary ? "secondary" : "primary";
    const char *dev  = slave ? "slave" : "master";

    ata_select_drive(ch, sel);
    ata_wait(ch);
    if (0 == in8(ch->io_base + 7)) {
        logk("%s-%s not exist!\n", chn, dev);
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
        logk("%s-%s is ATA-PI!\n", chn, dev);
        return;
    } else if ((0x3c == r4) && (0xc3 == r5)) {
        logk("%s-%s is SATA!\n", chn, dev);
        return;
    } else if (r4 || r5) {
        logk("%s-%s unknown ATA device type %x-%x\n", chn, dev, r4, r5);
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
        logk("%s-%s not ATA!\n", chn, dev);
        return;
    }

    // 我们要求 LBA 必须支持
    if (!(info[49] & 0x0200)) {
        logk("%s-%s no LBA support!\n", chn, dev);
        return;
    }

    // 确认设备存在，创建设备对象
    ata->flags  = 0;
    ata->flags |= secondary ? ATA_SECONDARY : 0;
    ata->flags |= slave     ? ATA_SLAVE     : 0;
    ata->flags |= (info[0]  & 0x0080) ? ATA_REMOVABLE : 0;
    ata->flags |= (info[49] & 0x0100) ? ATA_DMA       : 0;
    ata->flags |= (info[49] & 0x0200) ? ATA_LBA       : 0;
    ata->flags |= (info[83] & 0x0400) ? ATA_LBA48     : 0;

    kmemcpy(ata->serial,   &info[10], 20);
    kmemcpy(ata->revision, &info[23], 8);
    kmemcpy(ata->model,    &info[27], 40);

    logk("ATA %s-%s exist\n", chn, dev);
    logk("serial %.20s, rev %.8s, model %.40s\n",
        ata->serial, ata->revision, ata->model);

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

    // 向系统注册这个设备
    ata->blk.name = kernel_heap_mkstr("ata%d", secondary * 2 + slave + 1);
    ata->blk.ops = &ata_ops;
    add_block_dev(&ata->blk);

    // 通常硬盘有分区表，每个分区都是一个块设备
    partition_init(&ata->blk);
}

//------------------------------------------------------------------------------
// PIO 扇区读写
//------------------------------------------------------------------------------

static void ata_pio_read_sector(block_dev_t *blk, void *dst, uint64_t sector, uint64_t num) {
    ata_device_t *ata = containerof(blk, ata_device_t, blk);
    ata_channel_t *ch = &g_channels[(ata->flags & ATA_SECONDARY) ? 1 : 0];

    // 多个 ATA 设备可能来自同一个通道，互斥锁应该放在 channel 里面
    mutex_take(&ch->mutex, FOREVER);

    // bit 4 表示 slave
    // bit 6 表示 LBA
    // bit 5/7 已废弃，只有非常古老的 ATA 才用到，为了兼容将其置位
    uint8_t sel = (ata->flags & ATA_SLAVE) ? 0xf0 : 0xe0;
    uint8_t cmd;

    if (ATA_LBA48 & ata->flags) {
        cmd = 0x24; // READ_SECTORS_EXT
    } else { // LBA28，最高 4-bit 放在这里
        sel |= (sector >> 24) & 0x0f;
        cmd = 0x20; // READ_SECTORS
    }

    // 选择设备
    ata_select_drive(ch, sel);
    ata_wait(ch);

    // 写入起始扇区号
    if (ata->flags & ATA_LBA48) {
        out8(ch->io_base + 2, (num >> 8) & 0xff);       // sector count high
        out8(ch->io_base + 3, (sector >> 24) & 0xff);   // LBA 4
        out8(ch->io_base + 4, (sector >> 32) & 0xff);   // LBA 5
        out8(ch->io_base + 5, (sector >> 40) & 0xff);   // LBA 6
    }
    out8(ch->io_base + 2, num & 0xff);              // sector count low
    out8(ch->io_base + 3,  sector        & 0xff);   // LBA 1
    out8(ch->io_base + 4, (sector >>  8) & 0xff);   // LBA 2
    out8(ch->io_base + 5, (sector >> 16) & 0xff);   // LBA 3

    // 发送命令
    out8(ch->io_base + 7, cmd);

    uint16_t *buff = (uint16_t*)dst;

    // 必须逐个扇区读取，每读一个扇区都要等待 status
    // BSY 清除、ERR 清除、DRQ 置位
    for (unsigned s = 0; s < num; ++s) {
        // 如果出错，需要重置设备
        if (ata_wait_data(ch)) {
            logk("%s error when reading sector\n", ata->blk.name);
            ata_reset(ch);
            break;
        }

        // 读取一个扇区的数据
        // for (int i = 0; i < 256; ++i) {
        //     buff[i] = in16(ch->io_base);
        // }
        // buff += 256;

        // rep insw 执行后，rcx 和 rdi 的值会改变，需要声明
        size_t repeat = ata->blk.sec_size / sizeof(uint16_t);
        ASMV("rep insw" : "+D"(buff), "+c"(repeat) : "d"(ch->io_base) : "memory");
    }

    mutex_give(&ch->mutex);
}

static void ata_pio_write_sector(block_dev_t *blk, const void *src, uint64_t sector, uint64_t num) {
    ata_device_t *ata = containerof(blk, ata_device_t, blk);
    ata_channel_t *ch = &g_channels[(ata->flags & ATA_SECONDARY) ? 1 : 0];

    logk("ATA PIO writing sector %lx:%lx\n", sector, num);

    mutex_take(&ch->mutex, FOREVER);

    uint8_t sel; // = (ata->flags & ATA_SLAVE) ? 0xf0 : 0xe0; // 开启 LBA
    uint8_t cmd;

    if (ATA_LBA48 & ata->flags) {
        sel = (ata->flags & ATA_SLAVE) ? 0x50 : 0x40;
        cmd = 0x34; // WRITE_SECTORS_EXT
    } else { // LBA28
        sel = (ata->flags & ATA_SLAVE) ? 0xf0 : 0xe0;
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

    for (unsigned s = 0; s < num; ++s) {
        if (ata_wait_data(ch)) {
            logk("%s error when writing sector\n", ata->blk.name);
            ata_reset(ch);  // 如果出错，需要重置设备
            break;
        }

        // 写入数据
        const uint16_t *buff = (const uint16_t*)src;
        for (uint64_t i = 0; i < 256 * num; ++i) {
            out16(ch->io_base, buff[i]);
            cpu_pause();
        }
    }

    // 清缓存
    out8(ch->io_base + 7, 0xe7);
    mutex_give(&ch->mutex);
}

// 初始化
INIT_TEXT void ata_init() {
    g_channels[0].io_base      = ATA_PRIMARY_IO;
    g_channels[0].control_base = ATA_PRIMARY_CTL;
    g_channels[1].io_base      = ATA_SECONDARY_IO;
    g_channels[1].control_base = ATA_SECONDARY_CTL;

    mutex_init(&g_channels[0].mutex);
    mutex_init(&g_channels[1].mutex);

    ata_detect(&ata_devs[0], 0, 0); // primary master
    ata_detect(&ata_devs[1], 0, 1); // primary slave
    ata_detect(&ata_devs[2], 1, 0); // secondary master
    ata_detect(&ata_devs[3], 1, 1); // secondary slave
}
