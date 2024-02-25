// 端口模式的硬盘驱动
// 此模式性能很差，但所有 ATA 硬盘都支持
// 模拟器通常不提供 AHCI 支持，只能用 PIO 操作

#include <wheel.h>
#include <dev/pci.h>


// 一个 ATA 控制器可以支持两个设备，分别称为 primary 和 secondary
// 这些端口号和 IRQ 只是默认值，应该从 PCI 读取准确值
#define PRIMARY_PORT    0x1f0
#define PRIMARY_CONTROL 0x3f6
#define SECONDARY_PORT


// PCI 设备枚举到 ATA 时执行这个函数
void ata_probe(pci_device_t *dev) {
    (void)dev;
}
