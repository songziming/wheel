// 块设备及驱动管理器

#include <wheel.h>
#include <block.h>



// driver 还应该包括 pci 匹配标准，或者在注册 device 的时候同时指定 driver


// TODO 自旋锁保护？
static dlnode_t g_block_devices = DLNODE_INIT;
static dlnode_t g_block_drivers = DLNODE_INIT;


void block_dev_init() {
    dl_init_circular(&g_block_devices);
    dl_init_circular(&g_block_drivers);
}

void register_block_driver(blk_drv_t *drv) {
    ASSERT(!dl_contains(&g_block_drivers, &drv->dl));
    ASSERT(NULL != drv->read);
    ASSERT(NULL != drv->write);

    dl_insert_after(&drv->dl, &g_block_drivers);
}

void add_block_device(blk_dev_t *dev, blk_drv_t *drv) {
    ASSERT(!dl_contains(&g_block_devices, &dev->dl));
    ASSERT(dl_contains(&g_block_drivers, &drv->dl));

    dev->drv = drv;
    dl_insert_after(&dev->dl, &g_block_devices);
}

void block_read(blk_dev_t *dev, void *dst, uint32_t blk, uint32_t nblk) {
    dev->drv->read(dev, dst, blk, nblk);
}

void block_write(blk_dev_t *dev, const void *src, uint32_t blk, uint32_t nblk) {
    dev->drv->write(dev, src, blk, nblk);
}
