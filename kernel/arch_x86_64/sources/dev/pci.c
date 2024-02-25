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

static void pci_show_device(uint8_t bus, uint8_t dev, uint8_t func, uint32_t reg0) {
    pci_driver_t *driver = pci_get_driver(reg0);

    const char *name = driver ? driver->name : "?";
    uint16_t vendor = reg0 & 0xffff;
    uint16_t device = (reg0 >> 16) & 0xffff;

    uint32_t reg2 = g_pci_read(bus, dev, func, 8);
    uint8_t classcode = (reg2 >> 24) & 0xff;
    uint8_t subclass = (reg2 >> 16) & 0xff;
    uint8_t progif = (reg2 >> 8) & 0xff;

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

    klog("pci %x:%x:%x vendor/device=%04x/%04x, %s, class/subclass/prog=%x/%x/%x, %s\n",
        bus, dev, func, vendor, device, name, classcode, subclass, progif, subtype);
}


// 广度优先遍历各条 PCI 总线
static void pci_bfs(void (*action)(uint8_t, uint8_t, uint8_t, uint32_t)) {
    uint8_t buses[256];
    uint8_t num = 1;
    buses[0] = 0;

    while (num) {
        uint8_t bus = buses[--num];
        for (uint8_t dev = 0; dev < 32; ++dev) {
            for (uint8_t func = 0; func < 8; ++func) {
                uint32_t reg0 = g_pci_read(bus, dev, func, 0);
                if (0xffff == (reg0 & 0xffff)) {
                    if (0 == func) {
                        func = 8;
                    }
                    continue;
                }

                // 如果是 PCI-to-PCI bridge，则需要遍历
                uint32_t reg3 = g_pci_read(bus, dev, func, 12);
                uint8_t type = (reg3 >> 16) & 0x7f;
                if (1 == type) {
                    uint32_t reg6 = g_pci_read(bus, dev, func, 0x18);
                    buses[num++] = (reg6 >> 8) & 0xff;
                    continue;
                }

                action(bus, dev, func, reg0);
            }
        }
    }
}


// ata_pci.c
void ata_pci_init(uint8_t bus, uint8_t dev, uint8_t func);


void add_device(uint8_t bus, uint8_t dev, uint8_t func, uint32_t reg0) {
    uint32_t reg2 = g_pci_read(bus, dev, func, 8);
    uint8_t classcode = (reg2 >> 24) & 0xff;
    uint8_t subclass = (reg2 >> 16) & 0xff;
    // uint8_t progif = (reg2 >> 8) & 0xff;

    (void)reg0;

    if ((1 == classcode) && (1 == subclass)) {
        ata_pci_init(bus, dev, func);
    }
}


// TODO 检索到的设备添加到链表或平衡树里
void pci_enumerate() {
    pci_bfs(add_device);
}


static int show_pci(int argc, char *argv[]) {
    (void)argc;
    (void)argv;
    pci_bfs(pci_show_device);
    return 0;
}


static uint32_t pci_read(uint8_t bus, uint8_t dev, uint8_t func, uint8_t reg);
static void pci_write(uint8_t bus, uint8_t dev, uint8_t func, uint8_t reg, uint32_t data);


INIT_TEXT void pci_init(acpi_tbl_t *mcfg) {
    if (NULL == mcfg) {
        g_pci_read = pci_read;
        g_pci_write = pci_write;
    } else {
        klog("MCFG at %p, supports PCI-E\n", mcfg);
        g_pci_read = pci_read;
        g_pci_write = pci_write;
    }

    g_cmd_pci.name = "pci";
    g_cmd_pci.func = show_pci;
    shell_add_cmd(&g_cmd_pci);
}



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
