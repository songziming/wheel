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
// PCI 设备驱动数据库
//------------------------------------------------------------------------------

static rbtree_t g_drivers = RBTREE_INIT;

INIT_TEXT void pci_init(acpi_tbl_t *mcfg) {
    if (NULL == mcfg) {
        g_pci_read = pci_read;
        g_pci_write = pci_write;
        return;
    }

    klog("supports PCI-E!\n");
}

void pci_add_driver(pci_driver_t *drv) {
    ASSERT(NULL != drv);

    rbnode_t **link = &g_drivers.root;
    rbnode_t *parent = NULL;
    while (NULL != *link) {
        pci_driver_t *node = containerof(*link, pci_driver_t, rb);

        parent = *link;
        if (drv->key == node->key) {
            klog("pci driver for %04x:%04x already exist!\n", drv->vendor_id, drv->device_id);
            return;
        } else if (drv->key < node->key) {
            link = &parent->left;
        } else {
            link = &parent->right;
        }
    }

    rb_insert(&g_drivers, &drv->rb, parent, link);
}

pci_driver_t *pci_get_driver(uint32_t key) {
    rbnode_t *node = g_drivers.root;

    while (NULL != node) {
        pci_driver_t *driver = containerof(node, pci_driver_t, rb);
        if (driver->key == key) {
            return driver;
        }
        if (key < driver->key) {
            node = node->left;
        } else {
            node = node->right;
        }
    }

    return NULL;
}


//------------------------------------------------------------------------------
// 遍历总线上的所有设备
//------------------------------------------------------------------------------

void pci_add_device(uint8_t bus, uint8_t dev, uint8_t func, uint32_t reg0) {
    pci_driver_t *driver = pci_get_driver(reg0);
    if (NULL == driver) {
        uint16_t vendor = reg0 & 0xffff;
        uint16_t device = (reg0 >> 16) & 0xffff;
        klog("<%04x:%04x>", vendor, device);
        return;
    }

    klog("pci dev %d:%d:%d %s\n", bus, dev, func, driver->name);
}

void pci_walk_dev(uint8_t bus, uint8_t dev) {
    uint32_t reg0 = pci_read(bus, dev, 0, 0);
    if (0xffff == (reg0 & 0xffff)) {
        return; // 设备不存在
    }
    pci_add_device(bus, dev, 0, reg0);

    // 该设备可能有多个 function
    uint32_t reg2 = pci_read(bus, dev, 0, 12);
    if (reg2 & (1 << 23)) {
        for (uint8_t func = 1; func < 8; ++func) {
            reg0 = pci_read(bus, dev, func, 0);
            if (0xffff == (reg0 & 0xffff)) {
                continue;
            }
            pci_add_device(bus, dev, func, reg0);
        }
    }
}

void pci_walk_bus(uint8_t bus) {
    for (uint8_t dev = 0; dev < 32; ++dev) {
        pci_walk_dev(bus, dev);
    }
    klog("\n");
}

