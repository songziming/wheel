#include <wheel.h>

static iodev_t * pipe = NULL;

fdesc_t * ios_open(const char * filename) {
    // TODO: query vfs to get fs_node using filename
    if (NULL == pipe) {
        pipe = pipe_dev_create();
    }

    iodev_t * dev = pipe;
    // raw_spin_take(&dev->lock);
    // ++dev->refcount;
    // raw_spin_give(&dev->lock);

    fdesc_t * desc = kmem_alloc(sizeof(fdesc_t));
    desc->lock      = SPIN_INIT;
    desc->tid       = thiscpu_var(tid_prev);
    desc->dev       = dev;
    desc->dl_reader = DLNODE_INIT;
    desc->dl_writer = DLNODE_INIT;
    semaphore_init(&desc->sem, 1, 0);

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
    task_t  * tid = desc->tid;
    iodev_t * dev = desc->dev;
    iodrv_t * drv = dev->drv;

    assert(0 != len);
    assert(0 == thiscpu_var(int_depth));
    assert(tid == thiscpu_var(tid_prev));

    while (1) {
        // TODO: check if we got any pending signals to handle
        // TODO: check if we are timeout

        // check if we can read any content
        usize ret = drv->read(desc->dev, buf, len);
        if (ret) {
            return ret;
        }

        // cannot read any information, pend current task
        semaphore_take(&desc->sem, SEM_WAIT_FOREVER);
    }
}

usize ios_write(fdesc_t * desc, const u8 * buf, usize len) {
    task_t  * tid = desc->tid;
    iodev_t * dev = desc->dev;
    iodrv_t * drv = dev->drv;

    assert(0 != len);
    assert(0 == thiscpu_var(int_depth));
    assert(tid == thiscpu_var(tid_prev));

    while (1) {
        // TODO: check for signals
        // TODO: check for timeout

        // check if we can write into the file
        usize ret = drv->write(dev, buf, len);
        if (ret) {
            return ret;
        }

        // cannot write even one byte, pend current task
        raw_spin_take(&tid->lock);
        sched_stop(tid, TS_PEND);
        raw_spin_give(&tid->lock);
        task_switch();
    }
}
