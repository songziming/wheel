// VMware SVGA-II

#include <wheel.h>




void vmware_svga_init(uint8_t bus, uint8_t slot, uint8_t func) {
    uint16_t iobase = 0;    // 端口
    uint32_t framebuf = 0;  // MMIO
    uint32_t fiforegs = 0;  // MMIO

    uint32_t bar0 = g_pci_read(bus, slot, func, 0x10);
    if (bar0 & 1) {
        iobase = (uint16_t)(bar0 & ~3);
    } else {
        goto error;
    }

    uint32_t bar1 = g_pci_read(bus, slot, func, 0x14);
    if (bar1 & 1) {
        goto error;
    } else {
        framebuf = (uint16_t)(bar1 & ~15);
    }

    uint32_t bar2 = g_pci_read(bus, slot, func, 0x18);
    if (bar2 & 1) {
        goto error;
    } else {
        fiforegs = (uint32_t)(bar2 & ~15);
    }

    klog("svga port base %x, framebuf at %x, fifo at %x\n",
        iobase, framebuf, fiforegs);
    return;

error:
    klog("svga pci error\n");
}

