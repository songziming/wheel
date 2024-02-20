// 端口模式的硬盘驱动
// 此模式性能很差，但所有 ATA 硬盘都支持

#include <wheel.h>
#include <dev/pci.h>


// PCI 设备枚举到 ATA 时执行这个函数
void ata_probe(pci_device_t *dev) {
    (void)dev;
}
