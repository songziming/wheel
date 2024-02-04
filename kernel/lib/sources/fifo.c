#include <fifo.h>
#include <wheel.h>

// 循环队列
// 如果缓冲区满，要么停止写入，要么覆盖最早的数据

void fifo_init(fifo_t *fifo, void *buff, size_t len) {
    ASSERT(NULL != fifo);
    ASSERT(NULL != buff);
    ASSERT(0 == (len & (len - 1)));

    fifo->data = (uint8_t *)buff;
    fifo->size = len;
    fifo->r_head = 0;
    fifo->w_head = 0;
}


int fifo_is_full(fifo_t *fifo) {
    return (fifo->w_head - fifo->r_head) == fifo->size;
}

int fifo_is_empty(fifo_t *fifo) {
    return fifo->r_head == fifo->w_head;
}

size_t fifo_write(fifo_t *fifo, const void *ptr, size_t len, int force) {
    ASSERT(NULL != fifo);
    ASSERT(NULL != ptr);
    ASSERT(0 != len);

    uint8_t *src = (uint8_t *)ptr;
    uint8_t *dst = fifo->data;
    size_t mask = fifo->size - 1;

    // klog("writing %p:%ld to %p\n", src, len, dst);

    size_t copy = fifo->r_head + fifo->size - fifo->w_head;
    if (force || (copy > len)) {
        copy = len;
    }

    for (size_t i = 0; i < copy; ++i) {
        dst[(fifo->w_head + i) & mask] = src[i];
    }
    fifo->w_head += copy;
    if (fifo->w_head - fifo->r_head > fifo->size) {
        fifo->r_head = fifo->w_head - fifo->size;
    }

    return len;
}

size_t fifo_read(fifo_t *fifo, void *ptr, size_t len) {
    ASSERT(NULL != fifo);
    ASSERT(NULL != ptr);
    ASSERT(0 != len);

    uint8_t *src = fifo->data;
    uint8_t *dst = (uint8_t *)ptr;
    size_t copy = fifo->w_head - fifo->r_head; //, len);
    if (copy > len) {
        copy = len;
    }

    size_t mask = fifo->size - 1;
    for (size_t i = 0; i < copy; ++i) {
        dst[i] = src[(fifo->r_head + i) & mask];
    }
    fifo->r_head += copy;

    return copy;
}
