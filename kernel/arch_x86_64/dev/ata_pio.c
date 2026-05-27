#include <wheel.h>

// WIP

// PIO 是 ATA 最慢的访问方式，但驱动最简单

// ATA 有两种工作模式：
// compatibility mode，端口地址固定（1984 PC AT 之后从未改变），虚拟机使用这个
// native mode，需要解析PCI，读取 BAR 获得真正的端口号

// ATA 包括 primary 和 secondary 两个通道，每个通道可以关联主从两个设备
// 也就是最多支持四块硬盘

#define ATA_PRIMARY_IO      0x1F0
#define ATA_PRIMARY_CTL     0x3F6
#define ATA_SECONDARY_IO    0x170
#define ATA_SECONDARY_CTL   0x376


// 一个 channel 可以关联两个设备：master、slave
typedef struct ata_channel {
    uint16_t io_base;       // io 端口
    uint16_t control_base;  // ctrl 端口
    uint8_t irq;
} ata_channel_t;

