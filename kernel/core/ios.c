#include <wheel.h>

static iodev_t * pipe = NULL;

fdesc_t * ios_open(const char * filename) {
    // TODO: check if this file is already opened
    //       if so, just return the old fd

    // TODO: query vfs to get fs_node using filename
    //       then retrieve the iodev of that fs_node
    iodev_t * dev = NULL;
    if (0 == strcmp("pipe", filename)) {
        if (NULL == pipe) {
            pipe = pipe_dev_create();
        }
        dev = pipe;
    } else {
        return NULL;
    }

    assert(atomic32_inc(&dev->refcount) > 0);
    sema_take(&dev->sema, SEMA_WAIT_FOREVER);

    fdesc_t * desc = kmem_alloc(sizeof(fdesc_t));
    sema_init(&desc->sema, 1, 1);
    desc->pos       = 0;
    desc->dev       = dev;
    desc->drv       = dev->drv;
    desc->dl_reader = DLNODE_INIT;
    desc->dl_writer = DLNODE_INIT;

    // TODO: check read/write permission
    dl_push_tail(&dev->readers, &desc->dl_reader);
    dl_push_tail(&dev->writers, &desc->dl_writer);

    sema_give(&dev->sema);
    return desc;
}

void ios_close(fdesc_t * desc) {
    sema_take(&desc->sema, SEMA_WAIT_FOREVER);
    iodev_t * dev = desc->dev;
    iodrv_t * drv = desc->drv;
    assert(drv == dev->drv);

    // TODO: decrease refcount of desc->dev
    sema_take(&dev->sema, SEMA_WAIT_FOREVER);
    dl_remove(&dev->readers, &desc->dl_reader);
    dl_remove(&dev->writers, &desc->dl_writer);
    sema_give(&dev->sema);

    kmem_free(sizeof(fdesc_t), desc);
}

usize ios_read(fdesc_t * desc, u8 * buf, usize len) {
    assert(0 == thiscpu_var(int_depth));

    sema_take(&desc->sema, SEMA_WAIT_FOREVER);
    iodev_t * dev = desc->dev;
    iodrv_t * drv = desc->drv;
    assert(drv == dev->drv);

    if ((NULL == drv) || (NULL == drv->read)) {
        sema_take(&dev->sema, SEMA_WAIT_FOREVER);
        return 0;
    }
    usize ret = drv->read(desc, buf, len, &desc->pos);

    sema_give(&desc->sema);
    return ret;
}

usize ios_write(fdesc_t * desc, const u8 * buf, usize len) {
    assert(0 == thiscpu_var(int_depth));

    sema_take(&desc->sema, SEMA_WAIT_FOREVER);
    iodev_t * dev = desc->dev;
    iodrv_t * drv = desc->drv;
    assert(drv == dev->drv);

    if ((NULL == drv) || (NULL == drv->write)) {
        sema_take(&dev->sema, SEMA_WAIT_FOREVER);
        return 0;
    }
    usize ret = drv->write(desc, buf, len, &desc->pos);

    sema_give(&desc->sema);
    return ret;
}
