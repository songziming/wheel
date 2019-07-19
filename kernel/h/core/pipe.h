#ifndef CORE_PIPE_H
#define CORE_PIPE_H

#include <base.h>
// #include <core/spin.h>
// #include <mem/page.h>

// extern void  pipe_init   (pipe_t * pipe);
// extern void  pipe_destroy(pipe_t * pipe);
// extern usize pipe_read   (pipe_t * pipe, u8 * buf, usize len);
// extern usize pipe_write  (pipe_t * pipe, u8 * buf, usize len);

extern iodev_t * pipe_dev_create();
extern void      pipe_dev_delete(iodev_t * dev);

#endif // CORE_PIPE_H
