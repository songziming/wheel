#include <wheel.h>

static iodev_t * pipe = NULL;

fdesc_t * ios_open(const char * filename, int mode) {
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

    assert(atomic32_inc(&dev->ref) > 0);
    u32 key = irq_spin_take(&dev->spin);

    fdesc_t * desc = kmem_alloc(sizeof(fdesc_t));
    sema_init(&desc->sema, 1, 1);
    desc->dev       = dev;
    desc->drv       = dev->drv;
    desc->pos       = 0;
    desc->mode      = mode;
    desc->dl_reader = DLNODE_INIT;
    desc->dl_writer = DLNODE_INIT;

    if (mode & IOS_READ) {
        dl_push_tail(&dev->readers, &desc->dl_reader);
    }
    if (mode & IOS_WRITE) {
        dl_push_tail(&dev->writers, &desc->dl_writer);
    }

    irq_spin_give(&dev->spin, key);
    return desc;
}

void ios_close(fdesc_t * desc) {
    sema_take(&desc->sema, WAIT_FOREVER);
    iodev_t * dev = desc->dev;
    iodrv_t * drv = desc->drv;
    assert(drv == dev->drv);

    u32 key = irq_spin_take(&dev->spin);
    if (desc->mode & IOS_READ) {
        dl_remove(&dev->readers, &desc->dl_reader);
    }
    if (desc->mode & IOS_WRITE) {
        dl_remove(&dev->writers, &desc->dl_writer);
    }
    if (1 == atomic32_dec(&dev->ref)) {
        // TODO: delete this iodev by calling drv->close
    }
    irq_spin_give(&dev->spin, key);

    kmem_free(sizeof(fdesc_t), desc);
}

static void rw_timeout(task_t * tid, int * expired) {
    u32 key = irq_spin_take(&tid->spin);
    sched_cont(tid, TS_PEND);
    * expired = YES;
    irq_spin_give(&tid->spin, key);
}


//------------------------------------------------------------------------------
// file read function

// non blocking version, can be called during ISR
static usize ios_read_no_wait(fdesc_t * desc, u8 * buf, usize len) {
    if (OK != sema_take(&desc->sema, WAIT_FOREVER)) {
        return -1;
    }

    if (0 == (desc->mode & IOS_READ)) {
        sema_give(&desc->sema);
        return -1;
    }

    iodev_t * dev = desc->dev;
    iodrv_t * drv = desc->drv;

    u32 key = irq_spin_take(&dev->spin);

    if ((drv != dev->drv) || (NULL == drv) || (NULL == drv->read)) {
        irq_spin_give(&dev->spin, key);
        sema_give(&desc->sema);
        return -1;
    }

    usize ret = drv->read(desc, buf, len, &desc->pos);
    irq_spin_give(&dev->spin, key);

    sema_give(&desc->sema);
    return ret;
}

// cannot be called under ISR
static usize ios_read_wait_forever(fdesc_t * desc, u8 * buf, usize len) {
    assert(0 == thiscpu_var(int_depth));

    if (OK != sema_take(&desc->sema, WAIT_FOREVER)) {
        return -1;
    }

    if (0 == (desc->mode & IOS_READ)) {
        sema_give(&desc->sema);
        return -1;
    }

    iodev_t * dev = desc->dev;
    iodrv_t * drv = desc->drv;

    u32 key = irq_spin_take(&dev->spin);

    if ((drv != dev->drv) || (NULL == drv) || (NULL == drv->read)) {
        irq_spin_give(&dev->spin, key);
        sema_give(&desc->sema);
        return -1;
    }

    usize ret = drv->read(desc, buf, len, &desc->pos);
    irq_spin_give(&dev->spin, key);

    sema_give(&desc->sema);
    return ret;
}

static usize ios_read_timeout(fdesc_t * desc, u8 * buf, usize len, int timeout) {
    return 0;
}

usize ios_read(fdesc_t * desc, u8 * buf, usize len, int timeout) {
    switch (timeout) {
    case 0:
        return ios_read_no_wait(desc, buf, len);
    case WAIT_FOREVER:
        return ios_read_wait_forever(desc, buf, len);
    default:
        return ios_read_timeout(desc, buf, len, timeout);
    }
}

//------------------------------------------------------------------------------
// file write function

// non blocking version, can be called during ISR
static usize ios_write_no_wait(fdesc_t * desc, const u8 * buf, usize len) {
    return 0;
}

static usize ios_write_wait_forever(fdesc_t * desc, const u8 * buf, usize len) {
    return 0;
}

static usize ios_write_timeout(fdesc_t * desc, const u8 * buf, usize len, int timeout) {
    return 0;
}

usize ios_write(fdesc_t * desc, const u8 * buf, usize len, int timeout) {
    assert(0 == thiscpu_var(int_depth));

    sema_take(&desc->sema, timeout);
    iodev_t * dev = desc->dev;
    iodrv_t * drv = desc->drv;
    assert(drv == dev->drv);

    if ((NULL == drv) || (NULL == drv->write)) {
        // sema_take(&dev->sema, timeout);
        return 0;
    }
    usize ret = drv->write(desc, buf, len, &desc->pos);

    sema_give(&desc->sema);
    return ret;
}
