#ifndef FIFO_H
#define FIFO_H

#include <def.h>

typedef struct fifo {
    uint8_t *data;
    size_t   size;
    size_t   r_head;
    size_t   w_head;
} fifo_t;

#define FIFO_INIT ((fifo_t){ NULL,0,0,0 })

void fifo_init(fifo_t *fifo, void *buff, size_t len);
int fifo_is_full(fifo_t *fifo);
int fifo_is_empty(fifo_t *fifo);
size_t fifo_write(fifo_t *fifo, const void *ptr, size_t len, int force);
size_t fifo_read(fifo_t *fifo, void *ptr, size_t len);

#endif // FIFO_H
