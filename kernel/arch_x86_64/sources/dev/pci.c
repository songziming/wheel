// 读写 PCI 总线上的设备
// PCI 是一种通用总线，并非 x86 专属
// 可以在这里提供基础读写接口，common 模块实现设备枚举逻辑

#include <dev/pci.h>
#include <cpu/rw.h>
#include <wheel.h>
#include <shell.h>



CONST uint32_t (*g_pci_read)(uint8_t, uint8_t, uint8_t, uint8_t) = NULL;
CONST void (*g_pci_write)(uint8_t, uint8_t, uint8_t, uint8_t, uint32_t) = NULL;


static rbtree_t g_drivers = RBTREE_INIT;
static shell_cmd_t g_cmd_pci;

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

    const char *name = driver ? driver->name : "?";
    uint16_t vendor = reg0 & 0xffff;
    uint16_t device = (reg0 >> 16) & 0xffff;
    // if (NULL == driver) {
    //     uint16_t vendor = reg0 & 0xffff;
    //     uint16_t device = (reg0 >> 16) & 0xffff;
    //     klog("<%04x:%04x>", vendor, device);
    //     return;
    // }

    uint32_t reg2 = pci_read(bus, dev, func, 8);
    uint8_t classcode = (reg2 >> 24) & 0xff;
    uint8_t subclass = (reg2 >> 16) & 0xff;
    uint8_t progif = (reg2 >> 8) & 0xff;

    uint32_t reg3 = pci_read(bus, dev, func, 12);
    uint8_t header = (reg3 >> 16) & 0x7f;

    // const char *type = "?";
    const char *subtype = "?";
    switch (classcode) {
    case 0:
        // type = "unclassified";
        break;
    case 1:
        // type = "mass storage controller";
        switch (subclass) {
        case 0: subtype = "SCSI bus controller"; break;
        case 1: subtype = "IDE controller"; break;
        case 2: subtype = "floppy disk controller"; break;
        case 3: subtype = "IPI controller"; break;
        case 4: subtype = "RAID controller"; break;
        case 5: subtype = "ATA controller"; break;
        case 6: subtype = "Serial ATA controller"; break;
        case 7: subtype = "Serial Attached SCSI controller"; break;
        case 8: subtype = "non-volatile memory controller"; break;
        default: subtype = "other"; break;
        }
        break;
    case 2:
        // type = "network controller";
        switch (subclass) {
        case 0: subtype = "ethernet controller"; break;
        case 1: subtype = "token ring controller"; break;
        case 2: subtype = "FDDI controller"; break;
        case 3: subtype = "ATM controller"; break;
        }
        break;
    case 3:
        // type = "display controller";
        switch (subclass) {
        case 0: subtype = "VGA compatible controller"; break;
        case 1: subtype = "XGA compatible controller"; break;
        case 2: subtype = "3D controller"; break;
        }
        break;
    case 6:
        // type = "bridge";
        switch (subclass) {
        case 0: subtype = "host bridge"; break;
        case 1: subtype = "ISA bridge"; break;
        case 2: subtype = "EISA bridge"; break;
        case 3: subtype = "MCA bridge"; break;
        case 4: subtype = "PCI-to-PCI bridge"; break;
        case 5: subtype = "PCMCIA bridge"; break;
        case 6: subtype = "NuBus bridge"; break;
        case 7: subtype = "CardBus bridge"; break;
        case 8: subtype = "RACEway bridge"; break;
        case 9: subtype = "PCI-to-PCI bridge"; break;
        case 10: subtype = "InfiniBand-to-PCI host bridge"; break;
        }
        break;
    case 0xc:
        // type = "serial";
        switch (subclass) {
        case 0: subtype = "firewire controller"; break;
        case 1: subtype = "ACCESS bus controller"; break;
        case 2: subtype = "SSA"; break;
        case 3: subtype = "USB controller"; break;
        case 4: subtype = "fibre channel"; break;
        case 5: subtype = "SMBus controller"; break;
        case 6: subtype = "InfiniBand controller"; break;
        case 7: subtype = "IPMI interface"; break;
        case 8: subtype = "SERCOS interface"; break;
        case 9: subtype = "CANbus controller"; break;
        }
        break;
    default:
        break;
    }

    klog("pci %x:%x:%x vendor/device=%04x/%04x, %s, class/subclass/prog=%x/%x/%x, %s",
        bus, dev, func, vendor, device, name, classcode, subclass, progif, subtype);

    // 如果是 PCI-to-PCI bus，需要读取下一个 bus 的编号
    if (1 == header) {
        uint32_t reg6 = pci_read(bus, dev, func, 0x18);
        uint8_t primary = reg6 & 0xff;
        uint8_t secondary = (reg6 >> 8) & 0xff;
        ASSERT(bus == primary);
        klog(", secondary bus %d\n", secondary);
    } else {
        klog("\n");
    }
}

void pci_walk_dev(uint8_t bus, uint8_t dev) {
    uint32_t reg0 = pci_read(bus, dev, 0, 0);
    if (0xffff == (reg0 & 0xffff)) {
        return; // 设备不存在
    }
    pci_add_device(bus, dev, 0, reg0);

    // 该设备可能有多个 function
    uint32_t reg3 = pci_read(bus, dev, 0, 12);
    if (reg3 & (1 << 23)) {
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
    // klog("\n");
}


static int show_pci(int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    // 可以只搜索 bus0，遇到
    for (int bus = 0; bus < 256; ++bus) {
        pci_walk_bus((uint8_t)bus);
    }
    return 0;
}


INIT_TEXT void pci_init(acpi_tbl_t *mcfg) {
    if (NULL == mcfg) {
        g_pci_read = pci_read;
        g_pci_write = pci_write;
    } else {
        klog("supports PCI-E!\n");
    }

    g_cmd_pci.name = "pci";
    g_cmd_pci.func = show_pci;
    shell_add_cmd(&g_cmd_pci);
}

