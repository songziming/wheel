// IDE 硬盘驱动，利用 pci.h 定义的 read/write 函数

// 通过 PCI 只能检测到 IDE controller，获取各通道的端口号和 IRQ
// 具体对硬盘的操作还要通过 ata_pio 实现

#include <dev/pci.h>
#include <debug.h>



// ata_pio.c
void ata_probe();


void ata_pci_init(uint8_t bus, uint8_t dev, uint8_t func) {
    ASSERT(NULL != g_pci_read);

    klog("found ATA PCI device %d:%d:%d\n", bus, dev, func);

    uint32_t reg2 = g_pci_read(bus, dev, func, 8);
    uint8_t progif = (reg2 >> 8) & 0xff;

    // 检查两个 channel 的工作模式
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

    // 切换到 PCI native mode
    reg2 &= ~0xff00;
    reg2 |= (uint32_t)progif << 8;
    g_pci_write(bus, dev, func, 8, reg2);

    if (progif & 8) {
        klog("secondary channel togglable\n");
    }

    if (progif & (1 << 7)) {
        klog("supports DMA!\n");
    }

    // 读取五个 BAR，获取各个 ATA 设备使用的端口
    // 如果某个 BAR 为零，说明这个设备使用默认端口

    // BAR0、BAR1 描述 channel0
    if (ch0_native) {
        uint16_t ch0_cmd = 0x1f0;
        uint16_t ch0_ctl = 0x3f6;

        uint32_t bar0 = g_pci_read(bus, dev, func, 0x10);
        if (0 != bar0) {
            ASSERT(bar0 & 1);
            ch0_cmd = (uint16_t)(bar0 & ~3);
        }

        uint32_t bar1 = g_pci_read(bus, dev, func, 0x14);
        if (0 != bar1) {
            ASSERT(bar1 & 1);
            ch0_ctl = (uint16_t)(bar1 & ~3);
        }

        klog("channel 0 cmd base 0x%x, ctl base 0x%x\n", ch0_cmd, ch0_ctl);
    }

    // BAR2、BAR3 描述 channel1
    if (ch1_native) {
        uint16_t ch1_cmd = 0x170;
        uint16_t ch1_ctl = 0x376;

        uint32_t bar2 = g_pci_read(bus, dev, func, 0x18);
        if (0 != bar2) {
            ASSERT(bar2 & 1);
            ch1_cmd = (uint16_t)(bar2 & ~3);
        }

        uint32_t bar3 = g_pci_read(bus, dev, func, 0x1c);
        if (0 != bar3) {
            ASSERT(bar3 & 1);
            ch1_ctl = (uint16_t)(bar3 & ~3);
        }

        klog("channel 0 cmd base 0x%x, ctl base 0x%x\n", ch1_cmd, ch1_ctl);
    }

    // BAR4 描述 bus master IDE
    uint32_t bar4 = g_pci_read(bus, dev, func, 0x20);
    if (bar4 & 1) {
        uint16_t ch0 = (uint16_t)(bar4 & ~3);
        uint16_t ch1 = ch0 + 8;
        klog("bus master IDE base 0x%x, 0x%x\n", ch0, ch1);
    } else {
        klog("no bus master IDE\n");
    }

    // 只要有一个 channel 处于 PCI native mode
    // 就应该读取 interrupt line，获取该设备的 IRQ
    // 两个 channel 共享同一个 IRQ
    if (ch0_native || ch1_native) {
        uint32_t regf = g_pci_read(bus, dev, func, 0x3c);
        klog("PIC IRQ number %d", regf & 0xff);
    }

    // 调用 PIO ATA
    ata_probe();
}
