#ifndef CORE_IOS_H
#define CORE_IOS_H

#include <base.h>
#include <core/sema.h>

typedef struct iodrv iodrv_t;
typedef struct iodev iodev_t;
typedef struct fdesc fdesc_t;
typedef struct task  task_t;

typedef fdesc_t * (* ios_open_t)  (iodev_t * dev);
typedef void      (* ios_close_t) (iodev_t * dev);
typedef usize     (* ios_read_t)  (iodev_t * dev,       u8 * buf, usize len);
typedef usize     (* ios_write_t) (iodev_t * dev, const u8 * buf, usize len);

// multiple iodev could use this driver
struct iodrv {
    // ios_open_t  open;
    // ios_close_t close;
    ios_read_t  read;
    ios_write_t write;
};

// multiple fdesc could link to this device
struct iodev {
    iodrv_t * drv;          // type of this device
    dllist_t  readers;      // list of readers
    dllist_t  writers;      // list of writers
};

// multiple task can access this descriptor
struct fdesc {
    spin_t    lock;         // fdesc can be shared between threads
    task_t  * tid;          // owner task
    iodev_t * dev;          // which device we've opened
    dlnode_t  dl_reader;    // node in the readers list
    dlnode_t  dl_writer;    // node in the writers list
    sema_t sem;
};

#define IODRV_INIT ((iodrv_t) { NULL, NULL })
#define IODEV_INIT ((iodev_t) { NULL, DLLIST_INIT, DLLIST_INIT })
#define FDESC_INIT ((fdesc_t) { NULL, DLNODE_INIT, DLNODE_INIT })

extern fdesc_t * ios_open (const char * filename);
extern void      ios_close(fdesc_t * desc);
extern usize     ios_read (fdesc_t * desc,       u8 * buf, usize len);
extern usize     ios_write(fdesc_t * desc, const u8 * buf, usize len);

#endif // CORE_IOS_H
