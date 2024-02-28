// PCI 设备管理
// TODO PCIe 有何区别？加入对其的支持

#include <wheel.h>
#include <shell.h>
#include <pci.h>




typedef struct pci_dev {
    dlnode_t dl;
    uint8_t  bus;
    uint8_t  dev;
    uint8_t  func;

    uint16_t vendor;
    uint16_t device;

    uint8_t  classcode;
    uint8_t  subclass;
    uint8_t  progif;
} pci_dev_t;



// 记录所有 PCI 设备
static dlnode_t g_pci_devs = DLNODE_INIT;

// PCI 配置空间读写函数，由 arch 提供
CONST pci_reader_t g_pci_read = NULL;
CONST pci_writer_t g_pci_write = NULL;

static shell_cmd_t g_cmd_pci;


//------------------------------------------------------------------------------
// 设备枚举，广度搜索，启动时执行一次
//------------------------------------------------------------------------------


static INIT_TEXT void add_device(uint8_t bus, uint8_t dev, uint8_t func, uint32_t reg0) {
    pci_dev_t *pci = kernel_heap_alloc(sizeof(pci_dev_t));
    if (NULL == pci) {
        klog("warning: cannot alloc pci dev\n");
        return;
    }

    pci->bus = bus;
    pci->dev = dev;
    pci->func = func;

    pci->vendor = reg0 & 0xffff;
    pci->device = (reg0 >> 16) & 0xffff;

    uint32_t reg2 = g_pci_read(bus, dev, func, 8);
    pci->classcode = (reg2 >> 24) & 0xff;
    pci->subclass = (reg2 >> 16) & 0xff;
    pci->progif = (reg2 >> 8) & 0xff;

    pci->dl = DLNODE_INIT;
    dl_insert_before(&pci->dl, &g_pci_devs);
}

INIT_TEXT void pci_enumerate() {
    ASSERT(NULL != g_pci_read);
    ASSERT(NULL != g_pci_write);

    dl_init_circular(&g_pci_devs);

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

                add_device(bus, dev, func, reg0);
            }
        }
    }
}


//------------------------------------------------------------------------------
// 信息显示
//------------------------------------------------------------------------------

static void pci_show_dev(pci_dev_t *dev) {
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

    klog("pci %x:%x:%x vendor=%04x device=%04x class/subclass/prog=%x/%x/%x %s\n",
        dev->bus, dev->dev, dev->func, dev->vendor, dev->device,
        dev->classcode, dev->subclass, dev->progif, subtype);
}

static int pci_show(int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    for (dlnode_t *i = g_pci_devs.next; i != &g_pci_devs; i = i->next) {
        pci_show_dev(containerof(i, pci_dev_t, dl));
    }

    return 0;
}




//------------------------------------------------------------------------------
// PCI 注册读写函数，由 arch 调用
//------------------------------------------------------------------------------

INIT_TEXT void pci_init(pci_reader_t reader, pci_writer_t writer) {
    ASSERT(NULL == g_pci_read);
    ASSERT(NULL == g_pci_write);
    ASSERT(NULL != reader);
    ASSERT(NULL != writer);

    g_pci_read = reader;
    g_pci_write = writer;

    g_cmd_pci.name = "pci";
    g_cmd_pci.func = pci_show;
    shell_add_cmd(&g_cmd_pci);
}

