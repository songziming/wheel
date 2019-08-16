#include <wheel.h>

//------------------------------------------------------------------------------
// IO device abstract class

iodev_t * iodev_retain(iodev_t * dev) {
    assert(atomic32_inc(&dev->ref) > 0);
    return dev;
}

void iodev_delete(iodev_t * dev) {
    if (1 == atomic32_dec(&dev->ref)) {
        dev->free(dev);
        return;
    }
}

//------------------------------------------------------------------------------
// open and close IO device

static iodev_t * tst_pipe = NULL;
static iodev_t * kbd_pipe = NULL;

fdesc_t * ios_open(const char * filename, int mode) {
    // TODO: check if this file is already opened
    //       if so, just return the old fd

    // TODO: query vfs to get fs_node using filename
    //       then retrieve the iodev of that fs_node
    iodev_t * dev = NULL;
    if (0 == strcmp("pipe", filename)) {
        if (NULL == tst_pipe) {
            tst_pipe = pipe_dev_create();
        }
        dev = tst_pipe;
    } else if (0 == strcmp("/dev/kbd", filename)) {
        if (NULL == kbd_pipe) {
            kbd_pipe = pipe_dev_create();
        }
        dev = kbd_pipe;
    } else if (0 == strcmp("/dev/tty", filename)) {
        dev = tty_get_instance();
    } else {
        return NULL;
    }

    dev = iodev_retain(dev);

    fdesc_t * desc = kmem_alloc(sizeof(fdesc_t));
    desc->sema = SEMA_FULL(1);
    desc->dev  = dev;
    desc->drv  = dev->drv;
    desc->pos  = 0;
    desc->mode = mode;

    return desc;
}

void ios_close(fdesc_t * desc) {
    sema_take(&desc->sema, WAIT_FOREVER);
    iodev_t * dev = desc->dev;
    iodrv_t * drv = desc->drv;
    assert(drv == dev->drv);

    iodev_delete(dev);
    kmem_free(sizeof(fdesc_t), desc);
}

//------------------------------------------------------------------------------
// file read write function

usize ios_read(fdesc_t * desc, void * buf, usize len) {
    sema_take(&desc->sema, WAIT_FOREVER);
    iodev_t * dev = desc->dev;
    iodrv_t * drv = desc->drv;
    assert(drv == dev->drv);

    if (desc->mode & IOS_READ) {
        usize ret = drv->read(dev, (u8 *) buf, len, &desc->pos);
        sema_give(&desc->sema);
        return ret;
    } else {
        sema_give(&desc->sema);
        return (usize) -1;
    }
}

usize ios_write(fdesc_t * desc, const void * buf, usize len) {
    sema_take(&desc->sema, WAIT_FOREVER);
    iodev_t * dev = desc->dev;
    iodrv_t * drv = desc->drv;
    assert(drv == dev->drv);

    if (desc->mode & IOS_WRITE) {
        usize ret = drv->write(dev, (const u8 *) buf, len, &desc->pos);
        sema_give(&desc->sema);
        return ret;
    } else {
        sema_give(&desc->sema);
        return (usize) -1;
    }
}

//------------------------------------------------------------------------------
// format text output

void fprintf(fdesc_t * desc, const char * fmt, ...) {
    static chat buf[1024];
    va_list args;
    va_start(args, fmt);
    usize num = vsnprintf(buf, 1024, fmt, args);
    va_end(args);
    ios_write(desc, buf, num);
}
