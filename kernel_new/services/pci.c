#include "pci.h"

#include <library/debug.h>
#include <memory/early_alloc.h>


// PCI 配置空间读写函数，由 arch 提供
CONST pci_reader_t g_pci_read = NULL;
CONST pci_writer_t g_pci_write = NULL;

// 记录所有 PCI 设备
static CONST dlnode_t g_pci_devs;




INIT_TEXT void pci_lib_init(pci_reader_t reader, pci_writer_t writer) {
    ASSERT(NULL == g_pci_read);
    ASSERT(NULL == g_pci_write);
    ASSERT(NULL != reader);
    ASSERT(NULL != writer);

    g_pci_read = reader;
    g_pci_write = writer;

    dl_init_circular(&g_pci_devs);
}

// 添加一个设备
static INIT_TEXT pci_dev_t *add_device(uint8_t bus, uint8_t slot, uint8_t func, uint32_t reg0) {
    pci_dev_t *dev = early_alloc_ro(sizeof(pci_dev_t));
    if (NULL == dev) {
        log("warning: cannot alloc pci dev\n");
        return NULL;
    }

    dev->bus  = bus;
    dev->slot = slot;
    dev->func = func;

    dev->vendor = reg0 & 0xffff;
    dev->device = (reg0 >> 16) & 0xffff;
    log("+ pci vendor=%04x device=%04x\n", dev->vendor, dev->device);

    uint32_t reg2 = g_pci_read(bus, slot, func, 8);
    dev->classcode = (reg2 >> 24) & 0xff;
    dev->subclass = (reg2 >> 16) & 0xff;
    dev->progif = (reg2 >> 8) & 0xff;

    dev->dl = DLNODE_INIT;
    dl_insert_before(&dev->dl, &g_pci_devs);
    return dev;
}

// 广度优先搜索
INIT_TEXT void pci_probe() {
    ASSERT(NULL != g_pci_read);
    ASSERT(NULL != g_pci_write);

    dl_init_circular(&g_pci_devs);

    uint8_t buses[256];
    uint8_t num = 1;
    buses[0] = 0;

    while (num) {
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

// 设备遍历
void pci_enumerate(void (*cb)(const pci_dev_t *dev)) {
    ASSERT(NULL != g_pci_devs.prev);
    ASSERT(NULL != g_pci_devs.next);
    ASSERT(NULL != cb);

    for (dlnode_t *i = g_pci_devs.next; i != &g_pci_devs; i = i->next) {
        cb(containerof(i, pci_dev_t, dl));
    }
}
