#include "fifo.h"
#include "kstring.h"
#include "format.h"
// #include <wheel.h>



// 循环队列，如果缓冲区满，要么停止写入，要么覆盖最早的数据
// 分配两倍的虚拟地址空间，前后两段映射到相同物理内存
// 这样操作 fifo 无需考虑越界问题


void fifo_init(fifo_t *fifo, void *buff, size_t size) {
    while (size & (size - 1)) {
        size &= size - 1;
    }
    fifo->data = buff;
    fifo->size = size;
    fifo->r_head = 0;
    fifo->w_head = 0;
}

size_t fifo_data_size(fifo_t *fifo) {
    return fifo->w_head - fifo->r_head;
}

size_t fifo_left_size(fifo_t *fifo) {
    return fifo->size + fifo->r_head - fifo->w_head;
}

int fifo_is_full(fifo_t *fifo) {
    return (fifo->w_head - fifo->r_head) == fifo->size;
}

int fifo_is_empty(fifo_t *fifo) {
    return fifo->r_head == fifo->w_head;
}


// 至少写入 min 字节，至多写入 max 字节，否则写入 0 字节
size_t fifo_write(fifo_t *fifo, const void *ptr, size_t min, size_t max) {
    size_t remain = fifo->r_head + fifo->size - fifo->w_head;
    if (remain < min) {
        return 0; // 空间不足，无法写入
    }
    if (remain > max) {
        remain = max;
    }

    size_t mask = fifo->size - 1;
    kmemcpy(&fifo->data[fifo->w_head & mask], ptr, remain);
    fifo->w_head += remain;
    return remain;
}


void fifo_force_write(fifo_t *fifo, const void *ptr, size_t len) {
    const uint8_t *src = (const uint8_t*)ptr;
    size_t mask = fifo->size - 1;

    // 不大可能，调用者使用有误
    // 如果写入超过了总长，则之后最后一段才能保留
    if (len > fifo->size) {
        src += len & ~mask;
        len &= mask;
    }

    kmemcpy(&fifo->data[fifo->w_head & mask], src, len);
    fifo->w_head += len;
    if (fifo->w_head - fifo->r_head > fifo->size) {
        fifo->r_head = fifo->w_head - fifo->size;
    }
}


// 至少读取 min 字节，至多读取 max 字节，否则读取 0 字节
size_t fifo_read(fifo_t *fifo, void *ptr, size_t min, size_t max) {
    size_t size = fifo->w_head - fifo->r_head;
    if (size < min) {
        return 0;
    }

    if (size > max) {
        size = max;
    }

    size_t mask = fifo->size - 1;
    kmemcpy(ptr, &fifo->data[fifo->r_head & mask], size);
    fifo->r_head += size;
    return size;
}





// fifo 也可以用于 log，为了避免拷贝，直接让 format 向 fifo-data 写数据
// 无条件覆盖早前的数据

static void fifo_flush(void *user, const char **s, size_t *len) {
    fifo_t *fifo = (fifo_t*)user;
    size_t mask = fifo->size - 1;
    fifo->w_head += *len;
    if (fifo->w_head - fifo->r_head > fifo->size) {
        fifo->r_head = fifo->w_head - fifo->size;
    }
    *s = &fifo->data[fifo->w_head & mask];
    *len = fifo->size;
}

void fifo_vprint(fifo_t *fifo, const char *fmt, va_list va) {
    // size_t remain = fifo->size + fifo->r_head - fifo->w_head;
    size_t mask = fifo->size - 1;
    format(&fifo->data[fifo->w_head & mask], fifo->size, fifo_flush, fifo, fmt, va);
}
