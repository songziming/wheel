#ifndef CACHED_H
#define CACHED_H

#include <block.h>

blk_dev_t *cached_block_create(blk_dev_t *blk);
INIT_TEXT void cached_block_driver_init();

#endif // CACHED_H
