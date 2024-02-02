#ifndef DEV_PCI_H
#define DEV_PCI_H

#include <def.h>
#include "acpi.h"
#include <rbtree.h>


// 定义在 PCI Firmware Specification，但该文档是收费的
// 我们只能参考 wiki.osdev.org
typedef struct mcfg {
    acpi_tbl_t  header;
    uint8_t     reserved1[8];
    struct {
        uint64_t    addr;
        uint16_t    pci_seg;
        uint8_t     pci_bus_start;
        uint8_t     pci_bus_end;
        uint32_t    reserved2;
    } confs[0];
} PACKED mcfg_t;


// PCI 设备驱动，由 common 注册
// TODO PCI 设备枚举和驱动注册也可以放在 common 部分，arch 仅提供 PCI 读写函数
// TODO 驱动也可以分两级，vendor 一级，其下的 device 放在一个链表中
//      vendor 名称字符串只需要保存一份
typedef struct pci_driver {
    rbnode_t rb; // 红黑树节点，按 vendor/device 建立搜索树
    uint16_t vendor_id;
    uint16_t device_id;
    const char *name;

    void (*probe)(uint8_t bus, uint8_t dev, uint8_t func); // 发现匹配的设备时调用
} pci_driver_t;


extern CONST uint32_t (*g_pci_read)(uint8_t, uint8_t, uint8_t, uint8_t);
extern CONST void (*g_pci_write)(uint8_t, uint8_t, uint8_t, uint8_t, uint32_t);

void pci_walk_bus(uint8_t bus);

void pci_add_driver(pci_driver_t *driver);
INIT_TEXT void pci_init(mcfg_t *mcfg);

#endif // DEEV_PCI_H
