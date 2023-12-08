// 用于系统启动阶段，pool、heap 尚不可用时动态分配内存，分配之后就无法回收

#include <early_heap.h>
#include <arch_interface.h>
#include <debug.h>


INIT_TEXT void early_heap_init(early_heap_t *heap, void *buff, void *end) {
    ASSERT(NULL != heap);
    ASSERT(NULL != buff);
    ASSERT(NULL != end);
    ASSERT(buff < end);

    size_t start = (size_t)buff;
    start +=   sizeof(size_t) - 1;
    start &= ~(sizeof(size_t) - 1);

    heap->buff = (uint8_t *)start;
    heap->ptr  = (uint8_t *)start;
    heap->end  = (uint8_t *)end;
}

// 备份了内存中的关键数据结构之后，就可以放开 buffer 的长度限制
INIT_TEXT void early_heap_set_end(early_heap_t *heap, void *end) {
    ASSERT(NULL != heap);
    ASSERT(NULL != end);
    ASSERT(heap->end <= (uint8_t *)end);

    heap->end = (uint8_t *)end;
}

// 获取结束位置，停用之后，之后的内存由 page-alloc 管理
INIT_TEXT void *early_heap_get_ptr(early_heap_t *heap) {
    ASSERT(NULL != heap);

    return heap->ptr;
}

INIT_TEXT void *early_heap_alloc(early_heap_t *heap, size_t size) {
    size +=   sizeof(size_t) - 1;
    size &= ~(sizeof(size_t) - 1);

    if (heap->ptr + size > heap->end) {
        return NULL;
    }

    void *p = heap->ptr;
    heap->ptr += size;
    return p;
}
