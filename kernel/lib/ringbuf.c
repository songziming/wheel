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

void ringbuf_init(ringbuf_t *rb, char *data, size_t size) {
    while (size & (size - 1)) {
        size &= size - 1;
    }
    rb->data = data;
    rb->size = size;
    rb->r_head = 0;
    rb->w_head = 0;
}



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
