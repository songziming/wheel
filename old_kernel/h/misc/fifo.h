#ifndef MISC_FIFO_H
#define MISC_FIFO_H

#include <base.h>
#include <misc/debug.h>

// kernel mode non blocking pipe interface
// caller should prevent race condition using lock

typedef struct fifo {
    u8  * data;
    usize size;
    usize r_offset;     // might exceeds `size`
    usize w_offset;     // might exceeds `size`
} fifo_t;

// size must be power of 2
#define FIFO_INIT(data, size) ((fifo_t) { (u8 *) (data), (size), 0, 0 })

extern int   fifo_is_full (fifo_t * fifo);
extern int   fifo_is_empty(fifo_t * fifo);
extern usize fifo_read    (fifo_t * fifo,       u8 * buf, usize len);
extern usize fifo_peek    (fifo_t * fifo,       u8 * buf, usize len);
extern usize fifo_write   (fifo_t * fifo, const u8 * buf, usize len, int force);

#endif // MISC_FIFO_H
