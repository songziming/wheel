// 物理内存范围管理、地址空间管理


#include <arch_mem.h>
#include <arch_api.h>
#include <debug.h>
#include <libk_string.h>


//------------------------------------------------------------------------------
// 启动阶段的内存分配
//------------------------------------------------------------------------------

typedef struct membuff {
    uint8_t *ptr;
    size_t size;
    size_t used;
} membuff_t;

static SECTION(".rotail") ALIGNED(16) uint8_t g_ro_area[EARLY_RO_BUFF_SIZE];
static SECTION(".rwtail") ALIGNED(16) uint8_t g_rw_area[EARLY_RW_BUFF_SIZE];
static INIT_DATA membuff_t g_ro_buff = { g_ro_area, EARLY_RO_BUFF_SIZE, 0 };
static INIT_DATA membuff_t g_rw_buff = { g_rw_area, EARLY_RW_BUFF_SIZE, 0 };

static INIT_TEXT void *membuff_grow(membuff_t *buff, size_t size) {
    if (buff->used + size >= buff->size) {
        return NULL;
    }
    size +=   sizeof(size_t) - 1;
    size &= ~(sizeof(size_t) - 1);
    uint8_t *p = &buff->ptr[buff->used];
    buff->used += size;
    return p;
}

INIT_TEXT void *early_alloc_ro(size_t size) {
    void *p = membuff_grow(&g_ro_buff, size);
    if (NULL == p) {
        dbg_print("early ro alloc buffer overflow!\n");
        cpu_halt();
    }
    return p;
}

INIT_TEXT void *early_alloc_rw(size_t size) {
    void *p = membuff_grow(&g_rw_buff, size);
    if (NULL == p) {
        dbg_print("early rw alloc buffer overflow!\n");
        cpu_halt();
    }
    return p;
}

INIT_TEXT size_t ram_map_extentof(size_t addr);

// 解锁长度限制，可以超过配置的 BUFF_SIZE，只要物理内存足够就能一直分配
// 只放开 rw_buff 的限制，因为 ro_buff 与 rw_buff 很可能处于同一个 ram-range
INIT_TEXT void early_alloc_unlock() {
    size_t rw_addr = (size_t)g_rw_area - KERNEL_TEXT_BASE;
    g_rw_buff.size = ram_map_extentof(rw_addr) - rw_addr;
}


//------------------------------------------------------------------------------
// 物理内存布局管理
//------------------------------------------------------------------------------

typedef struct ram_range {
    ram_type_t type;
    size_t addr;
    size_t end;
} ram_range_t;

static CONST int g_ram_map_len = 0;
static CONST ram_range_t *g_ram_map = NULL;

INIT_TEXT void ram_map_reserve(int num) {
    ASSERT(0 == g_ram_map_len);
    ASSERT(NULL == g_ram_map);

    if (num <= 0) {
        return;
    }

    g_ram_map_len = num;
    g_ram_map = early_alloc_ro(num * sizeof(ram_range_t));
    kmemset(g_ram_map, 0, num * sizeof(ram_range_t));
}

// 添加一段物理内存
INIT_TEXT void ram_map_set(int idx, ram_type_t type, size_t addr, size_t len) {
    ASSERT(NULL != g_ram_map);
    ASSERT(idx >= 0);
    ASSERT(idx < g_ram_map_len);

    g_ram_map[idx].type = type;
    g_ram_map[idx].addr = addr;
    g_ram_map[idx].end  = addr + len;
}

// 返回 addr 之后能连续访问到的最高地址
INIT_TEXT size_t ram_map_extentof(size_t addr) {
    ASSERT(NULL != g_ram_map);

    for (int i = 0; i < g_ram_map_len; ++i) {
        if (RAM_AVAILABLE != g_ram_map[i].type) {
            continue;
        }
        size_t start = g_ram_map[i].addr;
        size_t end = g_ram_map[i].end;
        if ((start <= addr) && (addr < end)) {
            return end;
        }
    }

    dbg_print("error: address %lx not in valid ram range\n", addr);
    return 0;
}

#if DEBUG

static INIT_TEXT const char *ram_type_str(ram_type_t type) {
    switch (type) {
    case RAM_RESERVED: return "reserved";
    case RAM_AVAILABLE: return "available";
    case RAM_RECLAIMABLE: return "reclaimable";
    }
}

INIT_TEXT void ram_map_show() {
    ASSERT(NULL != g_ram_map);

    for (int i = 0; i < g_ram_map_len; ++i) {
        size_t addr = g_ram_map[i].addr;
        size_t end  = g_ram_map[i].end;
        const char *type = ram_type_str(g_ram_map[i].type);
        dbg_print(" -> ram range: addr=0x%016zx, end=0x%016zx, type=%s\n", addr, end, type);
    }
}

#endif // DEBUG
