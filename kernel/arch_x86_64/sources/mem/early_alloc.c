// 启动过程中，内存管理启用前，临时内存分配方案
// 永久分配，不回收

#include <arch_mem.h>
#include <wheel.h>


#define ALIGN 16

static SECTION(".rotail") uint8_t g_ro_area[EARLY_RO_SIZE] ALIGNED(ALIGN);
static SECTION(".rwtail") uint8_t g_rw_area[EARLY_RW_SIZE] ALIGNED(ALIGN);

typedef struct buff {
    uint8_t *ptr;
    uint8_t *end;
} buff_t;

static INIT_DATA buff_t g_ro_buff = {
    .ptr = g_ro_area,
    .end = g_ro_area + sizeof(g_ro_area),
};

static INIT_DATA buff_t g_rw_buff = {
    .ptr = g_rw_area,
    .end = g_rw_area + sizeof(g_rw_area),
};

static INIT_TEXT void *buff_grow(buff_t *buff, size_t size) {
    if (buff->ptr + size > buff->end) {
        return NULL;
    }
    size +=   ALIGN - 1;
    size &= ~(ALIGN - 1);
    uint8_t *p = buff->ptr;
    buff->ptr += size;
    return p;
}

INIT_TEXT void *early_alloc_ro(size_t size) {
    void *p = buff_grow(&g_ro_buff, size);
    if (NULL == p) {
        klog("fatal: early ro alloc buffer overflow!\n");
        return NULL;
    }
    return p;
}

INIT_TEXT void *early_alloc_rw(size_t size) {
    void *p = buff_grow(&g_rw_buff, size);
    if (NULL == p) {
        klog("fatal: early rw alloc buffer overflow!\n");
        return NULL;
    }
    return p;
}


// 将 early_rw 可分配范围延长到所在内存的上限
INIT_TEXT void early_rw_unlock() {
    ASSERT(NULL != g_pmmap);
    ASSERT(g_pmmap_len > 0);

    size_t ptr = (size_t)g_rw_buff.ptr - KERNEL_TEXT_ADDR;
    pmrange_t *rng = pmmap_locate(ptr);
    ASSERT(NULL != rng);
    ASSERT(PM_AVAILABLE == rng->type);

    g_rw_buff.end = (uint8_t *)rng->end + KERNEL_TEXT_ADDR;
}


// 禁用临时内存分配
INIT_TEXT void early_alloc_disable() {
    g_ro_buff.end = g_ro_buff.ptr;
    g_rw_buff.end = g_rw_buff.ptr;
}
