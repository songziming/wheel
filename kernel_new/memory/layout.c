#include <early_alloc.h>
#include <wheel.h>


#define ALIGNMENT 16

static SECTION(".rotail") uint8_t g_ro_data[EARLY_RO_SIZE] ALIGNED(ALIGNMENT);
static SECTION(".rwtail") uint8_t g_rw_data[EARLY_RW_SIZE] ALIGNED(ALIGNMENT);

typedef struct buff {
    uint8_t *ptr;
    uint8_t *end;
} buff_t;

static INIT_DATA buff_t g_ro_buff = {
    .ptr = g_ro_data,
    .end = g_ro_data + sizeof(g_ro_data),
};

static INIT_DATA buff_t g_rw_buff = {
    .ptr = g_rw_data,
    .end = g_rw_data + sizeof(g_rw_data),
};

static INIT_TEXT void *buff_alloc(buff_t *buff, size_t n) {
    if (buff->ptr + n > buff->end) {
        return NULL;
    }

    n +=   ALIGNMENT - 1;
    n &= ~(ALIGNMENT - 1);

    uint8_t *cur = buff->ptr;
    buff->ptr += n;
    return cur;
}

INIT_TEXT void *early_alloc_ro(size_t n) {
    void *p = buff_alloc(&g_ro_buff, n);
    if (NULL == p) {
        log("fatal: %s failed allocating %d\n", __func__, n);
        return NULL;
    }
    return p;
}

INIT_TEXT void *early_alloc_rw(size_t n) {
    void *p = buff_alloc(&g_rw_buff, n);
    if (NULL == p) {
        log("fatal: %s failed allocating %d\n", __func__, n);
        return NULL;
    }
    return p;
}

// 将 early_rw 可分配范围延长到所在内存的上限
INIT_TEXT void early_rw_unlock() {
    // size_t ptr = (size_t)g_rw_buff.ptr - KERNEL_TEXT_ADDR;
    // pmrange_t *rng = pmmap_locate(ptr);
    // ASSERT(NULL != rng);
    // ASSERT(PM_AVAILABLE == rng->type);

    // g_rw_buff.end = (uint8_t *)rng->end + KERNEL_TEXT_ADDR;
}


// 禁用临时内存分配
INIT_TEXT void early_alloc_disable() {
    g_ro_buff.end = g_ro_buff.ptr;
    g_rw_buff.end = g_rw_buff.ptr;

#ifdef DEBUG
    klog("early-ro used 0x%zx, end=%p\n", (size_t)(g_ro_buff.end - g_ro_data), g_ro_buff.end);
    klog("early-rw used 0x%zx, end=%p\n", (size_t)(g_rw_buff.end - g_rw_data), g_rw_buff.end);
#endif
}
