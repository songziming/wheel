// 读写 PCI 总线上的设备

#include <dev/pci.h>
#include <wheel.h>
#include <cpu/rw.h>



//------------------------------------------------------------------------------
// PCI 读写配置空间
//------------------------------------------------------------------------------

#define CONFIG_ADDR 0xcf8
#define CONFIG_DATA 0xcfc

static uint32_t pci_read(uint8_t bus, uint8_t dev, uint8_t func, uint8_t reg) {
    ASSERT(dev < 32);
    ASSERT(func < 8);
    ASSERT(0 == (reg & 3));

    uint32_t addr = ((uint32_t)bus  << 16)
                  | ((uint32_t)dev  << 11)
                  | ((uint32_t)func <<  8)
                  |  (uint32_t)reg
                  | 0x80000000U;
    out32(CONFIG_ADDR, addr);
    return in32(CONFIG_DATA);
}

static void pci_write(uint8_t bus, uint8_t dev, uint8_t func, uint8_t reg, uint32_t data) {
    ASSERT(dev < 32);
    ASSERT(func < 8);
    ASSERT(0 == (reg & 3));

    uint32_t addr = ((uint32_t)bus  << 16)
                  | ((uint32_t)dev  << 11)
                  | ((uint32_t)func <<  8)
                  |  (uint32_t)reg
                  | 0x80000000U;
    out32(CONFIG_ADDR, addr);
    out32(CONFIG_DATA, data);
}

// TODO PCIe 使用 MMIO 读写配置空间，而不是端口
//      如果 ACPI 提供了 MCFG 表，说明支持 PCIe
// TODO 实现一套 pcie 读写函数，像 xAPIC 和 x2APIC 通过函数指针动态选择


//------------------------------------------------------------------------------
// 遍历总线上的设备
//------------------------------------------------------------------------------


void pci_parse_func(uint8_t bus, uint8_t dev, uint8_t func) {
    uint32_t reg2  = pci_read(bus, dev, func, 8);
    uint16_t ccode = (reg2 >> 16) & 0xffff;  // base and sub class code
    uint8_t  prog  = (reg2 >>  8) & 0xff;    // programming interface

    klog("found pci device 0x%04x, prog if %d.\n", ccode, prog);

    // TODO 准备一个 PCI 设备编号数据表，可以注册驱动程序
    //      探测到新的 PCI 设备就从数据表中获取驱动
}

void pci_walk_dev(uint8_t bus, uint8_t dev) {
    if (0xffff == (pci_read(bus, dev, 0, 0) & 0xffff)) {
        return; // 设备不存在
    }
    pci_parse_func(bus, dev, 0);

    // 该设备可能有多个 function
    if (pci_read(bus, dev, 0, 12) & (1 << 23)) {
        for (uint8_t func = 1; func < 8; ++func) {
            if (0xffff == (pci_read(bus, dev, 0, 0) & 0xffff)) {
                continue;
            }
            pci_parse_func(bus, dev, func);
        }
    }
}

void pci_walk_bus(uint8_t bus) {
    for (uint8_t dev = 0; dev < 32; ++dev) {
        pci_walk_dev(bus, dev);
    }
}
