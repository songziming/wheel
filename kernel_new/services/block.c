#include "block.h"

#include <library/debug.h>


// 块设备管理


static dlnode_t g_block_devices; // = DLNODE_INIT;
static dlnode_t g_block_drivers; // = DLNODE_INIT;




INIT_TEXT void block_device_lib_init() {
    dl_init_circular(&g_block_devices);
    dl_init_circular(&g_block_drivers);
}


//------------------------------------------------------------------------------
// 注册块设备驱动，注册块设备
//------------------------------------------------------------------------------

void register_block_driver(blk_drv_t *drv) {
    ASSERT(!dl_contains(&g_block_drivers, &drv->dl));
    ASSERT(NULL != drv->read);
    ASSERT(NULL != drv->write);

    dl_insert_before(&drv->dl, &g_block_drivers);
}

void add_block_device(blk_dev_t *dev, blk_drv_t *drv) {
    ASSERT(!dl_contains(&g_block_devices, &dev->dl));
    ASSERT(dl_contains(&g_block_drivers, &drv->dl));

    dev->drv = drv;
    dl_insert_before(&dev->dl, &g_block_devices);
}

blk_dev_t *get_block_device() {
    if (dl_is_lastone(&g_block_devices)) {
        return NULL;
    }

    return containerof(g_block_devices.next, blk_dev_t, dl);
}


//------------------------------------------------------------------------------
// 封装的块读写函数
//------------------------------------------------------------------------------

void block_read(blk_dev_t *dev, void *dst, uint32_t blk, uint32_t nblk) {
    dev->drv->read(dev, dst, blk, nblk);
}

void block_write(blk_dev_t *dev, const void *src, uint32_t blk, uint32_t nblk) {
    dev->drv->write(dev, src, blk, nblk);
}
