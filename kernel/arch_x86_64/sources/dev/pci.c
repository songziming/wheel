// 读写 PCI 总线上的设备
// PCI 是一种通用总线，并非 x86 专属
// 可以在这里提供基础读写接口，common 模块实现设备枚举逻辑

#include <dev/pci.h>
#include <wheel.h>
#include <cpu/rw.h>



CONST uint32_t (*g_pci_read)(uint8_t, uint8_t, uint8_t, uint8_t) = NULL;
CONST void (*g_pci_write)(uint8_t, uint8_t, uint8_t, uint8_t, uint32_t) = NULL;


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


// // 设备存在返回 1，不存在返回 0
// int pci_parse_func(uint8_t bus, uint8_t dev, uint8_t func) {
//     uint32_t reg0 = pci_read(bus, dev, func, 0);
//     uint16_t vendor = reg0 & 0xffff;
//     uint16_t device = reg0 >> 16;
//     if (0xffff == vendor) {
//         return 0;
//     }

//     uint32_t reg2  = pci_read(bus, dev, func, 8);
//     uint16_t ccode = (reg2 >> 16) & 0xffff;  // base and sub class code
//     uint8_t  prog  = (reg2 >>  8) & 0xff;    // programming interface

//     klog("found pci device 0x%04x, prog if %d.\n", ccode, prog);

//     // TODO 准备一个 PCI 设备编号数据表，可以注册驱动程序
//     //      探测到新的 PCI 设备就从数据表中获取驱动
// }

void pci_walk_dev(uint8_t bus, uint8_t dev) {
    uint32_t reg0 = pci_read(bus, dev, 0, 0);
    uint16_t vendor = reg0 & 0xffff;
    uint16_t device = (reg0 >> 16) & 0xffff;
    if (0xffff == vendor) {
        return; // 设备不存在
    }

    klog("+ pci vendor=%04x device=%04x %d:%d:0.\n", vendor, device, bus, dev);

    // 该设备可能有多个 function
    uint32_t reg2 = pci_read(bus, dev, 0, 12);
    if (reg2 & (1 << 23)) {
        for (uint8_t func = 1; func < 8; ++func) {
            reg0 = pci_read(bus, dev, func, 0);
            vendor = reg0 & 0xffff;
            device = (reg0 >> 16) & 0xffff;
            if (0xffff == vendor) {
                continue;
            }

            klog("+ pci vendor=%04x device=%04x %d:%d:%d.\n", vendor, device, bus, dev, func);
        }
    }
}

void pci_walk_bus(uint8_t bus) {
    for (uint8_t dev = 0; dev < 32; ++dev) {
        pci_walk_dev(bus, dev);
    }
}





static rbtree_t pci_drivers = RBTREE_INIT;

void pci_add_driver(pci_driver_t *drv) {
    klog("registering driver %04x:%04x (%s)\n", drv->vendor_id, drv->device_id, drv->name);

    // rbtree_insert(&pci_drivers, &drv->rb);
}

INIT_TEXT void pci_init(mcfg_t *mcfg) {
    if (NULL == mcfg) {
        g_pci_read = pci_read;
        g_pci_write = pci_write;
        return;
    }
}
