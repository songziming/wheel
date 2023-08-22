#ifndef DRVS_PCI_H
#define DRVS_PCI_H

#include <base.h>

extern u32  pci_read (u8 bus, u8 dev, u8 func, u8 reg);
extern void pci_write(u8 bus, u8 dev, u8 func, u8 reg, u32 data);

extern void pci_probe_all();

#endif // DRVS_PCI_H
