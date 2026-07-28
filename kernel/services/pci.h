#ifndef PCI_H
#define PCI_H

#include <dllist.h>

typedef uint32_t (*pci_reader_t)(uint8_t bus, uint8_t slot, uint8_t func, uint8_t reg);
typedef void (*pci_writer_t)(uint8_t bus, uint8_t slot, uint8_t func, uint8_t reg, uint32_t val);

extern CONST pci_reader_t g_pci_read;
extern CONST pci_writer_t g_pci_write;

typedef struct pci_dev {
    dlnode_t    dl;
    uint8_t     bus;
    uint8_t     slot;
    uint8_t     func;
    uint16_t    vendor;
    uint16_t    device;
    uint8_t     classcode;
    uint8_t     subclass;
    uint8_t     progif;
} pci_dev_t;

INIT_TEXT void pci_probe();
pci_dev_t *pci_find(uint16_t vendor, uint16_t device);
uint64_t pci_get_bar(pci_dev_t *dev, int idx, char *isio, char *prefetch);

#endif // PCI_H
