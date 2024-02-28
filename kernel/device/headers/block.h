#ifndef BLOCK_H
#define BLOCK_H

#include <def.h>
#include <dllist.h>

typedef struct blk_drv blk_drv_t;
typedef struct blk_dev blk_dev_t;

struct blk_drv {
    dlnode_t dl;
    void (*read)(blk_dev_t *dev, void *dst, uint32_t blk, uint32_t nblk);
    void (*write)(blk_dev_t *dev, const void *src, uint32_t blk, uint32_t nblk);
};

struct blk_dev {
    dlnode_t dl;
    blk_drv_t *drv;
    uint32_t sec_size;
    uint32_t sec_num;
};


void block_dev_init();
void register_block_driver(blk_drv_t *drv);
void add_block_device(blk_dev_t *dev, blk_drv_t *drv);


#endif // BLOCK_H
