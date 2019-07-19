#ifndef CORE_IOS_H
#define CORE_IOS_H

#include <base.h>

typedef struct iodrv iodrv_t;
typedef struct iodev iodev_t;
typedef struct fdesc fdesc_t;

struct iodrv {
    // fdesc_t * (* open)  (iodev_t * dev);
    // void      (* close) (iodev_t * dev);
    usize     (* read)  (iodev_t * dev,       u8 * buf, usize len);
    usize     (* write) (iodev_t * dev, const u8 * buf, usize len);
};

struct iodev {
    iodrv_t * drv;          // type of this device
    dllist_t  readers;      // list of readers
    dllist_t  writers;      // list of writers
};

struct fdesc {
    iodev_t * dev;          // which device we've opened
    dlnode_t  dl_reader;    // node in the readers list
    dlnode_t  dl_writer;    // node in the writers list
};

#define IODRV_INIT ((iodrv_t) { NULL, NULL, NULL, NULL })
#define IODEV_INIT ((iodev_t) { NULL, DLLIST_INIT, DLLIST_INIT, NULL })
#define FDESC_INIT ((fdesc_t) { NULL, DLNODE_INIT, DLNODE_INIT })

extern fdesc_t * ios_open (const char * filename);
extern void      ios_close(fdesc_t * desc);
extern usize     ios_read (fdesc_t * desc,       u8 * buf, usize len);
extern usize     ios_write(fdesc_t * desc, const u8 * buf, usize len);

#endif // CORE_IOS_H
