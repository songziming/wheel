#ifndef CORE_PIPE_H
#define CORE_PIPE_H

#include <base.h>
#include <core/spin.h>
#include <mem/page.h>

// plain pipe, non blocking
typedef struct pipe {
    spin_t   lock;
    pglist_t pages;
    usize    r_offset;  // must within pages.head [0, PAGE_SIZE-1]
    usize    w_offset;  // must within pages.tail [0, PAGE_SIZE-1]
} pipe_t;

extern void  pipe_init   (pipe_t * pipe);
extern void  pipe_destroy(pipe_t * pipe);
extern usize pipe_read   (pipe_t * pipe, u8 * buf, usize len);
extern usize pipe_write  (pipe_t * pipe, u8 * buf, usize len);

#endif // CORE_PIPE_H
