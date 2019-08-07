#ifndef CORE_IOS_H
#define CORE_IOS_H

#include <base.h>
#include <core/sema.h>

typedef struct iodrv iodrv_t;
typedef struct iodev iodev_t;
typedef struct fdesc fdesc_t;

// typedef void  (* ios_open_t)  (iodev_t * dev);
// typedef void  (* ios_close_t) (iodev_t * dev);
typedef usize (* ios_read_t)  (iodev_t * dev,       u8 * buf, usize len, usize * pos);
typedef usize (* ios_write_t) (iodev_t * dev, const u8 * buf, usize len, usize * pos);
typedef void  (* ios_lseek_t) (iodev_t * dev, usize pos, int delta);

typedef void  (* ios_free_t)  (iodev_t * dev);

// multiple iodev could use this driver
struct iodrv {
    ios_read_t  read;
    ios_write_t write;
    ios_lseek_t lseek;
};

// multiple fdesc could link to this device
struct iodev {
    int         ref;    // RCU counter
    ios_free_t  free;   // release function
    iodrv_t   * drv;
};

// multiple task can access this descriptor
struct fdesc {
    sema_t    sema;
    iodrv_t * drv;          // virtual table
    iodev_t * dev;          // which device we've opened
    usize     pos;
    int       mode;
};

extern iodev_t * iodev_retain(iodev_t * dev);
extern void      iodev_delete(iodev_t * dev);

#define IOS_READ    1
#define IOS_WRITE   2

extern fdesc_t * ios_open (const char * filename, int mode);
extern void      ios_close(fdesc_t * desc);
extern usize     ios_read (fdesc_t * desc,       void * buf, usize len);
extern usize     ios_write(fdesc_t * desc, const void * buf, usize len);

#endif // CORE_IOS_H
