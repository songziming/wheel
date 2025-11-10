#ifndef FIFO_H
#define FIFO_H

#include <def.h>

typedef struct fifo {
    uint8_t *data;
    size_t   size;
    size_t   r_head;
    size_t   w_head;
} fifo_t;

void fifo_init(fifo_t *fifo, void *buff, size_t size);

size_t fifo_data_size(fifo_t *fifo);
size_t fifo_left_size(fifo_t *fifo);

int  fifo_is_full(fifo_t *fifo);
int  fifo_is_empty(fifo_t *fifo);

size_t fifo_write(fifo_t *fifo, const void *ptr, size_t min, size_t max);
void fifo_force_write(fifo_t *fifo, const void *ptr, size_t len);

size_t fifo_read(fifo_t *fifo, void *ptr, size_t min, size_t max);

#endif // FIFO_H
