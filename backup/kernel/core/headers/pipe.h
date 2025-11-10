#ifndef PIPE_H
#define PIPE_H

#include <def.h>
#include <fifo.h>
#include <dllist.h>
#include "spin.h"
#include "timer.h"

typedef struct pipe {
    spin_t   spin;
    fifo_t   fifo;
    dlnode_t writers; // 排队等待写入数据的
    dlnode_t readers; // 排队等待读取数据的
} pipe_t;

void pipe_init(pipe_t *pipe, void *buff, size_t size);

size_t pipe_write(pipe_t *pipe, const void *src, size_t min, size_t max, tick_t timeout);
void pipe_force_write(pipe_t *pipe, const void *src, size_t len);

size_t pipe_read(pipe_t *pipe, void *dst, size_t min, size_t max, tick_t timeout);

#endif // PIPE_H
