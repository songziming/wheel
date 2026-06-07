#include "pci.h"
#include <early_alloc.h>
#include <dllist.h>
#include <debug.h>

#include <kshell.h>
#include <console.h>


CONST pci_reader_t g_pci_read = NULL;
CONST pci_writer_t g_pci_write = NULL;

// 启动之后，设备链表就固定了，不会动态增删
static CONST dlnode_t g_pci_head;

// 我们不支持 PCIe 热插拔，分配只读即可
static void add_device(uint8_t bus, uint8_t slot, uint8_t func, uint32_t reg0) {
    pci_dev_t *dev = early_alloc_ro(sizeof(pci_dev_t));
    dev->bus = bus;
    dev->slot = slot;
    dev->func = func;

    dev->vendor = reg0 & 0xffff;
    dev->device = (reg0 >> 16) & 0xffff;
    uint32_t reg2 = g_pci_read(bus, slot, func, 8);
    dev->classcode = (reg2 >> 24) & 0xff;
    dev->subclass = (reg2 >> 16) & 0xff;
    dev->progif = (reg2 >> 8) & 0xff;

    logk("+ pci vendor=%04x device=%04x, cls=%x, subcls=%x, progif=%x\n",
        dev->vendor, dev->device, dev->classcode, dev->subclass, dev->progif);
    dl_insert_before(&dev->dl, &g_pci_head);
}

// 广度优先搜索，查找所有设备
// 需要在启动初期调用，因为要用到 early-alloc
INIT_TEXT void pci_probe() {
    int num = 1;
    uint8_t buses[256]; // LIFO, next buses to probe
    buses[0] = 0;

    dl_init_circular(&g_pci_head);

    while (num > 0) {
        uint8_t bus = buses[--num];
        for (uint8_t slot = 0; slot < 32; ++slot) {
            for (uint8_t func = 0; func < 8; ++func) {
                uint32_t reg0 = g_pci_read(bus, slot, func, 0);
                if (0xffff == (reg0 & 0xffff)) {
                    if (0 == func) {
                        func = 8;
                    }
                    continue;
                }

                // 如果是 PCI-to-PCI bridge，则也需要遍历
                uint32_t reg3 = g_pci_read(bus, slot, func, 12);
                uint8_t type = (reg3 >> 16) & 0x7f;
                if (1 == type) {
                    uint32_t reg6 = g_pci_read(bus, slot, func, 0x18);
                    buses[num++] = (reg6 >> 8) & 0xff;
                    continue;
                }

                add_device(bus, slot, func, reg0);
            }
        }
    }
}

pci_dev_t *pci_find(uint16_t vendor, uint16_t device) {
    for (dlnode_t *dl = g_pci_head.next; dl != &g_pci_head; dl = dl->next) {
        pci_dev_t *dev = containerof(dl, pci_dev_t, dl);
        if ((vendor == dev->vendor) && (device == dev->device)) {
            return dev;
        }
    }
    return NULL;
}

/// @brief 读取 BAR
/// @param[out] isio    类型是 IO
/// @param[out] prefetch 支持预取
// TODO 将 bar 缓存下来保存在 pci_dev 里面？
uint64_t pci_get_bar(pci_dev_t *dev, int idx, char *isio, char *prefetch) {
    uint32_t off = 0x10 + idx * 4;
    uint32_t bar = g_pci_read(dev->bus, dev->slot, dev->func, off);
    if (bar & 1) {
        // IO-space BAR
        *isio = 1;
        return bar & 0xfffffffcU;
    } else {
        // memory-space BAR
        *isio = 0;
        *prefetch = (bar & 8) ? 1 : 0;
        uint32_t type = (bar >> 1) & 3;
        if (2 == type) { // 64-bit BAR
            uint32_t upper = g_pci_read(dev->bus, dev->slot, dev->func, off + 4);
            return ((uint64_t)upper << 32) + (bar & 0xfffffff0U);
        }
        return bar & 0xfffffff0U;
    }
}

//------------------------------------------------------------------------------

#if !defined(UNIT_TEST)

static void pci_show_dev(const pci_dev_t *dev) {
    ASSERT(NULL != dev);

    // const char *type = "?";
    const char *subtype = "?";
    switch (dev->classcode) {
    case 0:
        // type = "unclassified";
        break;
    case 1:
        // type = "mass storage controller";
        switch (dev->subclass) {
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
        switch (dev->subclass) {
        case 0: subtype = "ethernet controller"; break;
        case 1: subtype = "token ring controller"; break;
        case 2: subtype = "FDDI controller"; break;
        case 3: subtype = "ATM controller"; break;
        }
        break;
    case 3:
        // type = "display controller";
        switch (dev->subclass) {
        case 0: subtype = "VGA compatible controller"; break;
        case 1: subtype = "XGA compatible controller"; break;
        case 2: subtype = "3D controller"; break;
        }
        break;
    case 6:
        // type = "bridge";
        switch (dev->subclass) {
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
        switch (dev->subclass) {
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

    console_printf("pci %x:%x:%x vendor=%04x device=%04x "
            "class/subclass/prog=%x/%x/%x %s\n",
        dev->bus, dev->slot, dev->func, dev->vendor, dev->device,
        dev->classcode, dev->subclass, dev->progif, subtype);
}

static void show_pci() {
    for (dlnode_t *dl = g_pci_head.next; dl != &g_pci_head; dl = dl->next) {
        pci_dev_t *dev = containerof(dl, pci_dev_t, dl);
        pci_show_dev(dev);
    }
}

KSHELL_CMD("pci", show_pci);

#endif // !defined(UNIT_TEST)
