// 端口模式的硬盘驱动（俗称的 IDE）
// 此模式性能很差，但所有 ATA 硬盘都支持
// 模拟器通常不提供 AHCI 支持，只能用 PIO 操作

#include <wheel.h>
#include <dev/pci.h>
#include <cpu/rw.h>



// 主板通常有两个 ATA 接口通道，分别称为 primary 和 secondary（还可以更多，但很少见）
// 每个通道又可以接两个设备，称为 master 和 slave，通过 device 寄存器 bit 4 区分
// 这里我们只用到 primary channel

// primary channel 寄存器端口
#define CH0_CMD 0x1f0   // 共八个
#define CH0_CTL 0x3f6   // 仅一个

// control 寄存器各 bit
#define CTL_IEN     2   // 禁用该设备发送中断
#define CTL_RESET   4   // 设备重置（需保持至少 5us）

// 状态寄存器各 bit
#define STT_BUSY    0x80
#define STT_READY   0x40
#define STT_DRQ     0x08    // data request


// 向硬盘发送命令的方法：将命令内容写入 command regs，在写 control reg 发送



// 每次向 ATA 发送命令，都要等待 400ns，然后不断读 status，直到 BUSY bit 清零
// 读取 15 次，保留最后一次读取结果，差不多就是 400ns
static void ata_delay() {
    for (int i = 0; i < 14; ++i) {
        in8(CH0_CTL);
    }
    while(0 == (in8(CH0_CTL) & STT_READY)) {}
}



// static void ata_reset() {
//     out8(CH0_CTL, CTL_RESET);
//     ata_delay();
//     out8(CH0_CTL, 0);
//     ata_delay();
// }







// 发送 IDENTITY 命令，获取硬盘参数
static void ata_identity(uint16_t cmd, uint16_t ctl, int slave) {
    out8(cmd + 6, slave ? 0xb0 : 0xa0); // device
    ata_delay();
}


void ata_init(uint16_t cmd, uint16_t ctl, int slave) {
    ata_identity(cmd, ctl, slave);

    out8(ctl, CTL_RESET | CTL_IEN);
    ata_delay();

    out8(ctl, CTL_IEN);
    while (ata_delay() & STT_BUSY) {}
}


// 如果 PCI 设备枚举未找到任何 AHCI 再调用这个函数
// 或者是找到了 PCI IDE 设备，却不支持 PCI native mode
void ata_probe() {
    ata_init(0x1f0, 0x3f6, 0);  // primary master
    ata_init(0x1f0, 0x3f6, 1);  // primary slave
    ata_init(0x170, 0x376, 0);  // secondary master
    ata_init(0x170, 0x376, 1);  // secondary slave
}
