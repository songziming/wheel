// IDE 硬盘驱动，利用 pci.h 定义的 read/write 函数

#include <dev/pci.h>
#include <debug.h>




void ata_pci_init(uint8_t bus, uint8_t dev, uint8_t func) {
    klog("found ATA PCI device %d:%d:%d\n", bus, dev, func);

    uint32_t reg2 = g_pci_read(bus, dev, func, 8);
    uint8_t progif = (reg2 >> 8) & 0xff;

    // 检查两个 channel 的工作模式

    const char *primary = (progif & 1) ? "PCI native" : "compatibility";
    const char *secondary = (progif & 4) ? "PCI native" : "compatibility";
    klog("primary channel in %s mode, secondary channel in %s mode\n", primary, secondary);

    if (progif & 2) {
        klog("primary channel togglable\n");
    }

    if (progif & 8) {
        klog("secondary channel togglable\n");
    }

    if (progif & (1 << 7)) {
        klog("supports DMA!\n");
    }


    // 读取五个 BAR，获取各个 ATA 设备使用的端口
    uint32_t bar0 = pci_read(bus, dev, func, 0x10);
    uint32_t bar1 = pci_read(bus, dev, func, 0x14);
    uint32_t bar2 = pci_read(bus, dev, func, 0x18);
    uint32_t bar3 = pci_read(bus, dev, func, 0x1c);
    uint32_t bar4 = pci_read(bus, dev, func, 0x20);
    klog("bars %x %x %x %x %x\n", bar0, bar1, bar2, bar3, bar4);
}
