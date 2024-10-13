// 设备初始化
// 大部分设备驱动定义在 arch 模块之外，这里调用，未用到的驱动因为 gc-sections 删除
// 还有一些驱动独属于当前 arch，放在 arch 模块内，也在这里初始化

#include <common.h>
#include "cpu/rw.h"
#include <devices/acpi.h>
#include <devices/hpet.h>
#include <devices/i8042.h>

#include <library/debug.h>
#include <library/string.h>
#include <services/pci.h>

#include <x/keyboard.h>



INIT_TEXT void tty_init(); // x/tty.c


typedef struct pci_conf {
    uint64_t    addr;
    uint16_t    seg;
    uint8_t     bus_start;
    uint8_t     bus_end;
    uint32_t    reserved;
} PACKED pci_conf_t;

typedef struct mcfg {
    acpi_tbl_t  header;
    uint64_t    reserved;
    pci_conf_t  spaces[0];
} PACKED mcfg_t;


//------------------------------------------------------------------------------
// PCI support
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

static INIT_TEXT void show_mcfg(mcfg_t *mcfg) {
    int num = (mcfg->header.length - sizeof(acpi_tbl_t)) / sizeof(pci_conf_t);
    log("has PCIe support! %d conf spaces\n", num);
    for (int i = 0; i < num; ++i) {
        pci_conf_t *conf = &mcfg->spaces[i];
        log("- PCIe conf space at 0x%zx, seg=%d, bus=%d-%d\n",
            conf->addr, conf->seg, conf->bus_start, conf->bus_end);
    }
}

// 识别 PCI 设备，调用驱动
static INIT_TEXT void install_pci_dev(const pci_dev_t *dev) {
    if ((0x15ad == dev->vendor) && (0x0405 == dev->device)) {
        // vmware_svga_init(dev->bus, dev->slot, dev->func);
        log("vmware display\n");
        return;
    }

    if ((1 == dev->classcode) && (1 == dev->subclass)) {
        // ata_pci_lib_init(dev);
        log("ata storage\n");
        return;
    }

    log("unknown PCI vendor=%04x, device=%04x\n", dev->vendor, dev->device);
}


//------------------------------------------------------------------------------
// 分三个阶段初始化设备
//------------------------------------------------------------------------------

// 可以使用 early_alloc
INIT_TEXT void pre_memory_hwinit() {
    mcfg_t *mcfg = (mcfg_t *)acpi_table_find("MCFG", 0);
    if (mcfg) {
        show_mcfg(mcfg);
    }
    pci_lib_init(pci_read, pci_write);
    pci_probe();    // 检测所有 PCI 设备，但是暂不初始化

    hpet_init();
}

// 可以分配物理页、映射虚拟地址
INIT_TEXT void pre_task_hwinit() {
    keyboard_init();
    i8042_init();
}

// 可以创建任务
INIT_TEXT void post_task_hwinit() {
    pci_enumerate(install_pci_dev);
    // tty_init();
}
