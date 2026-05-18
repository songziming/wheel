#ifndef RINGBUF_H
#define RINGBUF_H

#include <stddef.h>

typedef struct ringbuf {
    char    *data;
    size_t   size;
    size_t   r_head;
    size_t   w_head;
} ringbuf_t;

void ringbuf_init(ringbuf_t *rbuf, char *data, size_t size);
size_t ringbuf_write(ringbuf_t *rbuf, const void *ptr, size_t min, size_t max);
void ringbuf_force_write(ringbuf_t *rbuf, const void *ptr, size_t len);
size_t ringbuf_read(ringbuf_t *rbuf, void *ptr, size_t min, size_t max);

#endif // RINGBUF_H
