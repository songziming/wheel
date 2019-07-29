#include <wheel.h>

static iodev_t * pipe = NULL;

fdesc_t * ios_open(const char * filename) {
    // TODO: query vfs to get fs_node using filename
    if (NULL == pipe) {
        pipe = pipe_dev_create();
    }

    iodev_t * dev = pipe;

    fdesc_t * desc = kmem_alloc(sizeof(fdesc_t));
    desc->lock      = SPIN_INIT;
    // desc->tid       = thiscpu_var(tid_prev);
    desc->dev       = dev;
    desc->dl_reader = DLNODE_INIT;
    desc->dl_writer = DLNODE_INIT;
    sema_init(&desc->sem, 1, 0);

    // TODO: check read/write permission
    dl_push_tail(&dev->readers, &desc->dl_reader);
    dl_push_tail(&dev->writers, &desc->dl_writer);

    return desc;
}

void ios_close(fdesc_t * desc) {
    preempt_lock();
    raw_spin_take(&desc->lock);

    // TODO: if other tasks are pending on this fdesc, unpend them
    kmem_free(sizeof(fdesc_t), desc);
    preempt_unlock();
}

usize ios_read(fdesc_t * desc, u8 * buf, usize len) {
    assert(0 == thiscpu_var(int_depth));

    preempt_lock();
    raw_spin_take(&desc->lock);
    iodev_t * dev = desc->dev;
    iodrv_t * drv = dev->drv;
    raw_spin_give(&desc->lock);
    preempt_unlock();

    return drv->read(dev, buf, len);
}

usize ios_write(fdesc_t * desc, const u8 * buf, usize len) {
    assert(0 == thiscpu_var(int_depth));

    preempt_lock();
    raw_spin_take(&desc->lock);
    iodev_t * dev = desc->dev;
    iodrv_t * drv = dev->drv;
    raw_spin_give(&desc->lock);
    preempt_unlock();

    return drv->write(dev, buf, len);
}
