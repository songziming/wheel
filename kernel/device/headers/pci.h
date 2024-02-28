#ifndef PCI_H
#define PCI_H

#include <def.h>

typedef uint32_t (*pci_reader_t)(uint8_t bus, uint8_t slot, uint8_t func, uint8_t reg);
typedef void (*pci_writer_t)(uint8_t bus, uint8_t slot, uint8_t func, uint8_t reg, uint32_t val);

extern CONST pci_reader_t g_pci_read;
extern CONST pci_writer_t g_pci_write;

INIT_TEXT void pci_init(pci_reader_t reader, pci_writer_t writer);
INIT_TEXT void pci_enumerate();

#endif // PCI_H
