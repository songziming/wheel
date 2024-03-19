#ifndef DEV_ATA_H
#define DEV_ATA_H

#include <pci.h>

INIT_TEXT void ata_driver_init();
INIT_TEXT void ata_pci_lib_init(const pci_dev_t *dev);

#endif // DEV_ATA_H
