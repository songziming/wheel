#ifndef PCI_H
#define PCI_H

#include <library/dllist.h>

typedef struct pci_dev {
    dlnode_t dl;
    uint8_t  bus;
    uint8_t  slot;
    uint8_t  func;

    uint16_t vendor;
    uint16_t device;

    uint8_t  classcode;
    uint8_t  subclass;
    uint8_t  progif;
} pci_dev_t;

typedef uint32_t (*pci_reader_t)(uint8_t bus, uint8_t slot, uint8_t func, uint8_t reg);
typedef void (*pci_writer_t)(uint8_t bus, uint8_t slot, uint8_t func, uint8_t reg, uint32_t val);

extern CONST pci_reader_t g_pci_read;
extern CONST pci_writer_t g_pci_write;

INIT_TEXT void pci_lib_init(pci_reader_t reader, pci_writer_t writer);
INIT_TEXT void pci_probe();

void pci_enumerate(void (*cb)(const pci_dev_t *dev));

void pci_show();

#endif // PCI_H
