#include <wheel.h>

fdesc_t * ios_open(const char * filename) {
    fdesc_t * desc = kmem_alloc(sizeof(fdesc_t));
}

void ios_close(fdesc_t * desc) {
    kmem_free(sizeof(fdesc_t), desc);
}

usize ios_read(fdesc_t * desc, u8 * buf, usize len) {
    iodev_t * dev = desc->dev;
    iodrv_t * drv = dev->drv;
    return drv->read(dev, buf, len);
}

usize ios_write(fdesc_t * desc, const u8 * buf, usize len) {
    iodev_t * dev = desc->dev;
    iodrv_t * drv = dev->drv;
    return drv->write(dev, buf, len);
}
