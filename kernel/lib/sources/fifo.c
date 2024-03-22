#include <fifo.h>
#include <wheel.h>

// 循环队列
// 如果缓冲区满，要么停止写入，要么覆盖最早的数据

void fifo_init(fifo_t *fifo, void *buff, size_t size) {
    ASSERT(NULL != fifo);
    ASSERT(NULL != buff);
    ASSERT(0 == (size & (size - 1)));

    fifo->data = (uint8_t *)buff;
    fifo->size = size;
    fifo->r_head = 0;
    fifo->w_head = 0;
}


int fifo_is_full(fifo_t *fifo) {
    return (fifo->w_head - fifo->r_head) == fifo->size;
}

int fifo_is_empty(fifo_t *fifo) {
    return fifo->r_head == fifo->w_head;
}


// 至少写入 min 字节，至多写入 max 字节，否则写入 0 字节
size_t fifo_write(fifo_t *fifo, const void *ptr, size_t min, size_t max) {
    ASSERT(NULL != fifo);
    ASSERT(NULL != ptr);
    ASSERT(min <= max);
    ASSERT(0 != min);

    size_t remain = fifo->r_head + fifo->size - fifo->w_head;
    if (remain < min) {
        return 0; // 空间不足，无法写入
    }

    if (remain > max) {
        remain = max;
    }

    const uint8_t *src = (const uint8_t *)ptr;
    size_t mask = fifo->size - 1;

    size_t start = fifo->w_head & mask;
    size_t end = (fifo->w_head + remain) & mask;

    if (start < end) {
        memcpy(fifo->data + start, src, remain);
    } else {
        size_t size1 = fifo->size - start;
        memcpy(fifo->data + start, src, size1);
        memcpy(fifo->data, src + size1, end);
    }

    fifo->w_head += remain;
    return remain;
}


void fifo_force_write(fifo_t *fifo, const void *ptr, size_t len) {
    ASSERT(NULL != fifo);
    ASSERT(NULL != ptr);
    ASSERT(0 != len);

    const uint8_t *src = (const uint8_t *)ptr;
    size_t mask = fifo->size - 1;

    // 不大可能，调用者使用有误
    if (len > fifo->size) {
        src += len & ~mask;
        len &= mask;
    }

    size_t start = fifo->w_head & mask;
    size_t end = (fifo->w_head + len) & mask;

    if (start < end) {
        memcpy(fifo->data + start, src, len);
    } else {
        size_t size1 = fifo->size - start;
        memcpy(fifo->data + start, src, size1);
        memcpy(fifo->data, src + size1, end);
    }

    fifo->w_head += len;
    if (fifo->w_head - fifo->r_head > fifo->size) {
        fifo->r_head = fifo->w_head - fifo->size;
    }
}


// 至少读取 min 字节，至多读取 max 字节，否则读取 0 字节
size_t fifo_read(fifo_t *fifo, void *ptr, size_t min, size_t max) {
    ASSERT(NULL != fifo);
    ASSERT(NULL != ptr);
    ASSERT(min <= max);
    ASSERT(0 != min);

    size_t size = fifo->w_head - fifo->r_head;
    if (size < min) {
        return 0;
    }

    if (size > max) {
        size = max;
    }

    uint8_t *dst = (uint8_t *)ptr;
    size_t mask = fifo->size - 1;

    size_t start = fifo->r_head & mask;
    size_t end = (fifo->r_head + size) & mask;

    if (start < end) {
        memcpy(dst, fifo->data + start, size);
    } else {
        size_t size1 = fifo->size - start;
        memcpy(dst, fifo->data + start, size1);
        memcpy(dst + size1, fifo->data, end);
    }

    fifo->r_head += size;
    return size;
}
