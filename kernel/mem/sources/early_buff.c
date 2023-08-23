// 启动阶段的内存分配策略，只分配不回收

#include <early_buff.h>
#include <debug.h>

INIT_TEXT void early_buff_init(early_buff_t *buff, void *ptr, void *end) {
    ASSERT(NULL != buff);
    ASSERT(NULL != ptr);
    ASSERT(NULL != end);
    ASSERT(ptr < end);

    size_t start = (size_t)ptr;
    start +=   sizeof(size_t) - 1;
    start &= ~(sizeof(size_t) - 1);

    buff->ptr = (uint8_t *)start;
    buff->cur = (uint8_t *)start;
    buff->end = (uint8_t *)end;
}

INIT_TEXT void early_buff_set_end(early_buff_t *buff, void *end) {
    ASSERT(NULL != buff);
    buff->end = (uint8_t *)end;
}

INIT_TEXT void *early_buff_get_cur(early_buff_t *buff) {
    ASSERT(NULL != buff);
    return buff->cur;
}

INIT_TEXT void *early_buff_alloc(early_buff_t *buff, size_t size) {
    ASSERT(NULL != buff);

    size +=   sizeof(size_t) - 1;
    size &= ~(sizeof(size_t) - 1);

    if (buff->cur + size > buff->end) {
        return NULL;
    }
    void *p = buff->cur;
    buff->cur += size;
    return p;
}
