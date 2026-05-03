#ifndef ARCH_X86_64_DEV_PCI_H
#define ARCH_X86_64_DEV_PCI_H

#include <wheel.h>

uint32_t pci_read(uint8_t bus, uint8_t dev, uint8_t func, uint8_t reg);
void pci_write(uint8_t bus, uint8_t dev, uint8_t func, uint8_t reg, uint32_t data);

#endif // ARCH_X86_64_DEV_PCI_H
