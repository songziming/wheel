#ifndef PIPE_H
#define PIPE_H

#include <common.h>
#include "fifo.h"
#include <library/dllist.h>
#include <library/spin.h>
#include <proc/tick.h>

typedef struct pipe {
    spin_t   spin;
    fifo_t   fifo;
    dlnode_t writers; // 排队等待写入数据的
    dlnode_t readers; // 排队等待读取数据的
} pipe_t;

void pipe_init(pipe_t *pipe, void *buff, size_t size);

size_t pipe_write(pipe_t *pipe, const void *src, size_t min, size_t max, int timeout);
void pipe_force_write(pipe_t *pipe, const void *src, size_t len);

size_t pipe_read(pipe_t *pipe, void *dst, size_t min, size_t max, int timeout);

#endif // PIPE_H
