#ifndef PARTITION_H
#define PARTITION_H

#include <block.h>

void partitions_init(blk_dev_t *dev);
INIT_TEXT void partition_driver_init();

#endif // PARTITION_H
