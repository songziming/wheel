#ifndef RINGBUF_H
#define RINGBUF_H

#include <stddef.h>

typedef struct ringbuf {
    char    *data;
    size_t   size;
    size_t   r_head;
    size_t   w_head;
} ringbuf_t;

void ringbuf_init(ringbuf_t *rb, char *data, size_t size);

#endif // RINGBUF_H
