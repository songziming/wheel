#include "ringbuf.h"
#include "format.h"
#include <wheel.h>

// 环形缓冲区，可以用于 log-buffer
// 每个 task 需要打印时，提前预留 ringbuf 内部的空间，使用 format 格式化字符串
// 将格式化结果放入 ringbuf 之后，调用 framebuf/serial 输出

// 因为用在 log，这个代码内部不能使用 assert，不能调用 log

// size 必须是 2 的幂，r_head..w_head 之间的数据是有效的
// ringbuf  [......r_head----------w_head.....]
// wrap:    [-----w_head......r_head----------]

// TODO 可以让 OS 在虚拟空间里分配2倍长度的空间，前后两段映射到相同的物理内存
//      这样我们就无需关心 buffer 末端的 rollback 问题（Linux kfifo 就是这么做的）
//      虚拟地址空间很大，值得多分配

void ringbuf_init(ringbuf_t *rbuf, char *data, size_t size) {
    while (size & (size - 1)) {
        size &= size - 1;
    }
    rbuf->data = data;
    rbuf->size = size;
    rbuf->r_head = 0;
    rbuf->w_head = 0;
}


size_t ringbuf_data_size(ringbuf_t *rbuf) {
    return rbuf->w_head - rbuf->r_head;
}
size_t ringbuf_left_size(ringbuf_t *rbuf) {
    return rbuf->size + rbuf->r_head - rbuf->w_head;
}
int ringbuf_is_full(ringbuf_t *rbuf) {
    return (rbuf->w_head - rbuf->r_head) == rbuf->size;
}
int ringbuf_is_empty(ringbuf_t *rbuf) {
    return rbuf->r_head == rbuf->w_head;
}


// 至少写入 min 字节，至多写入 max 字节，否则写入 0 字节
size_t ringbuf_write(ringbuf_t *rbuf, const void *ptr, size_t min, size_t max) {
    ASSERT(NULL != rbuf);
    ASSERT(NULL != ptr);
    ASSERT(min <= max);
    ASSERT(0 != min);

    size_t remain = rbuf->r_head + rbuf->size - rbuf->w_head;
    if (remain < min) {
        return 0; // 空间不足，无法写入
    }

    if (remain > max) {
        remain = max;
    }

    const uint8_t *src = (const uint8_t*)ptr;
    size_t mask = rbuf->size - 1;

    size_t start = rbuf->w_head & mask;
    size_t end = (rbuf->w_head + remain) & mask;

    if (start < end) {
        kmemcpy(rbuf->data + start, src, remain);
    } else {
        size_t size1 = rbuf->size - start;
        kmemcpy(rbuf->data + start, src, size1);
        kmemcpy(rbuf->data, src + size1, end);
    }

    rbuf->w_head += remain;
    return remain;
}


void ringbuf_force_write(ringbuf_t *rbuf, const void *ptr, size_t len) {
    ASSERT(NULL != rbuf);
    ASSERT(NULL != ptr);
    ASSERT(0 != len);

    const uint8_t *src = (const uint8_t*)ptr;
    size_t mask = rbuf->size - 1;

    // 不大可能，调用者使用有误
    if (len > rbuf->size) {
        src += len & ~mask;
        len &= mask;
    }

    size_t start = rbuf->w_head & mask;
    size_t end = (rbuf->w_head + len) & mask;

    if (start < end) {
        kmemcpy(rbuf->data + start, src, len);
    } else {
        size_t size1 = rbuf->size - start;
        kmemcpy(rbuf->data + start, src, size1);
        kmemcpy(rbuf->data, src + size1, end);
    }

    rbuf->w_head += len;
    if (rbuf->w_head - rbuf->r_head > rbuf->size) {
        rbuf->r_head = rbuf->w_head - rbuf->size;
    }
}


// 至少读取 min 字节，至多读取 max 字节，否则读取 0 字节
size_t ringbuf_read(ringbuf_t *rbuf, void *ptr, size_t min, size_t max) {
    ASSERT(NULL != rbuf);
    ASSERT(NULL != ptr);
    ASSERT(min <= max);
    ASSERT(0 != min);

    size_t size = rbuf->w_head - rbuf->r_head;
    if (size < min) {
        return 0;
    }

    if (size > max) {
        size = max;
    }

    uint8_t *dst = (uint8_t*)ptr;
    size_t mask = rbuf->size - 1;

    size_t start = rbuf->r_head & mask;
    size_t end = (rbuf->r_head + size) & mask;

    if (start < end) {
        kmemcpy(dst, rbuf->data + start, size);
    } else {
        size_t size1 = rbuf->size - start;
        kmemcpy(dst, rbuf->data + start, size1);
        kmemcpy(dst + size1, rbuf->data, end);
    }

    rbuf->r_head += size;
    return size;
}



// TODO 预先划分一段空间，慢慢往里写数据，写完再调用 notify-write-done
//      这样可以减少 kmemcpy 次数
// TODO 需要三个指针：
//  - prepare_head 表示预先分配的位置
//  - w_head 表示完成数据写入的位置
//  - r_head 表示已经读取的位置
// 申请到的 buf 可能长度不足 len
char *ringbuf_write_prepare(ringbuf_t *rbuf, size_t len, size_t *got) {
    char *ptr = rbuf->data + rbuf->w_head;
    rbuf->w_head += len;
    *got = len;
    return ptr;
}

void ringbuf_write_done(ringbuf_t *rbuf, size_t len) {
    rbuf->w_head += len;
}




#if 0

// format 部分渲染完成的回调，可能回调多次
// s, len 作为 in/out 参数：输入为已输出的子串，输出为下一段 buf 的地址和长度
static void ringbuf_got_str(void *user, const char **s, size_t *len) {
    ringbuf_t *rb = (ringbuf_t*)user;
    rb->w_head += *len;
    *s   = &rb->data[rb->w_head];
    *len = rb->size - rb->w_head;
}

void ringbuf_print(ringbuf_t *rb, char *fmt, ...) {
    size_t max = rb->size - rb->w_head; // w-head 到末尾最大空间
    va_list va;
    va_start(va, fmt);
    format(&rb->data[rb->w_head], max, ringbuf_got_str, rb, fmt, va);
    va_end(va);
}

#endif
