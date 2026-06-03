#include "fifo.h"
#include "kstring.h"
#include "format.h"
// #include <wheel.h>



// 循环队列，如果缓冲区满，要么停止写入，要么覆盖最早的数据


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

size_t fifo_write(fifo_t *fifo, const void *ptr, size_t min, size_t max) {
    size_t remain = fifo->r_head + fifo->size - fifo->w_head;
    if (remain < min) {
        return 0; // 空间不足，无法写入
    }
    if (remain > max) {
        remain = max;
    }

    size_t mask = fifo->size - 1;
    size_t start = fifo->w_head & mask;
    fifo->w_head += remain;

    if (start + remain <= fifo->size) {
        kmemcpy(&fifo->data[start], ptr, remain);
    } else {
        size_t first = fifo->size - start;
        kmemcpy(&fifo->data[start], ptr, first);
        kmemcpy(fifo->data, (char*)ptr + first, remain - first);
    }

    return remain;
}


void fifo_force_write(fifo_t *fifo, const void *ptr, size_t len) {
    const char *src = (const char*)ptr;
    size_t mask = fifo->size - 1;

    // 不大可能，调用者使用有误
    // 如果写入超过了总长，则之后最后一段才能保留
    if (len > fifo->size) {
        src += len & ~mask;
        len &= mask;
    }

    size_t start = fifo->w_head & mask;
    fifo->w_head += len;
    if (fifo->w_head - fifo->r_head > fifo->size) {
        fifo->r_head = fifo->w_head - fifo->size;
    }

    if (start + len <= fifo->size) {
        kmemcpy(&fifo->data[start], src, len);
    } else {
        size_t first = fifo->size - start;
        kmemcpy(&fifo->data[start], src, first);
        kmemcpy(fifo->data, src + first, len - first);
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
    size_t start = fifo->r_head & mask;
    fifo->r_head += size;
    if (start + size <= fifo->size) {
        kmemcpy(ptr, &fifo->data[start], size);
    } else {
        size_t first = fifo->size - start;
        kmemcpy(ptr, &fifo->data[start], first);
        kmemcpy((char*)ptr + first, fifo->data, size - first);
    }
    return size;
}


//------------------------------------------------------------------------------
// 格式化字符串，结果放在 fifo 里面，覆盖最老的数据，可用于系统日志
//------------------------------------------------------------------------------

static void fifo_flush(void *user, const char **s, size_t *len) {
    fifo_t *fifo = (fifo_t*)user;
    fifo->w_head += *len;

    size_t start = fifo->w_head & (fifo->size - 1);
    *s = &fifo->data[start];
    *len = fifo->size - start;
}

// 本函数用于 logk，里面不能使用 logk 与 assert
void fifo_vprint(fifo_t *fifo, const char *fmt, va_list va, log_func_t cb) {
    size_t mask = fifo->size - 1;
    size_t start = fifo->w_head & mask;
    size_t remain = fifo->size - start;
    size_t len = format(&fifo->data[start], remain, fifo_flush, fifo, fmt, va);

    // 如果覆盖了更早的未读取的数据
    if (fifo->w_head - fifo->r_head > fifo->size) {
        fifo->r_head = fifo->w_head - fifo->size;
    }

    // 如果传入了回调，则调用打印
    // 注意这不是读取数据，r_head 并未改变
    if ((NULL != cb) && (len > 0)) {
        if (len > fifo->size) {
            len = fifo->size;
        }
        size_t from = (fifo->w_head - len) & mask;
        size_t to = fifo->w_head & mask;
        if (from >= to) {
            cb(&fifo->data[from], fifo->size - from);
            cb(fifo->data, to);
        } else {
            cb(&fifo->data[from], to - from);
        }
    }
}
