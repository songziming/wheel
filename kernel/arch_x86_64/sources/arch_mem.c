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

// 解锁长度限制，可以超过配置的 BUFF_SIZE，只要物理内存足够就能一直分配
// 只放开 rw_buff 的限制，因为 ro_buff 与 rw_buff 很可能处于同一个 ram-range
INIT_TEXT void early_alloc_unlock() {
    size_t rw_addr = (size_t)g_rw_area - KERNEL_TEXT_BASE;
    g_rw_buff.size = rammap_extentof(rw_addr) - rw_addr;
}


//------------------------------------------------------------------------------
// 物理内存布局管理
//------------------------------------------------------------------------------

typedef struct ram_range {
    ram_type_t type;
    size_t addr;
    size_t end;
} ram_range_t;

static CONST int g_rammap_len = 0;
static CONST ram_range_t *g_rammap = NULL;

INIT_TEXT void rammap_reserve(int num) {
    ASSERT(0 == g_rammap_len);
    ASSERT(NULL == g_rammap);

    if (num <= 0) {
        return;
    }

    g_rammap_len = num;
    g_rammap = early_alloc_ro(num * sizeof(ram_range_t));
    kmemset(g_rammap, 0, num * sizeof(ram_range_t));
}

// 添加一段物理内存
INIT_TEXT void rammap_set(int idx, ram_type_t type, size_t addr, size_t len) {
    ASSERT(NULL != g_rammap);
    ASSERT(idx >= 0);
    ASSERT(idx < g_rammap_len);

    g_rammap[idx].type = type;
    g_rammap[idx].addr = addr;
    g_rammap[idx].end  = addr + len;
}

// 返回 addr 所在的内存范围的截止地址，用于确定 early_alloc_buff 的增长极限
// 此时 ACPI 内存尚未回收，只考虑 AVAILABLE 类型的内存范围
INIT_TEXT size_t rammap_extentof(size_t addr) {
    ASSERT(NULL != g_rammap);

    for (int i = 0; i < g_rammap_len; ++i) {
        if (RAM_AVAILABLE != g_rammap[i].type) {
            continue;
        }
        size_t start = g_rammap[i].addr;
        size_t end = g_rammap[i].end;
        if ((start <= addr) && (addr < end)) {
            return end;
        }
    }

    dbg_print("error: address %lx not in valid ram range\n", addr);
    return 0;
}

// 检查一段内存是否与可用内存范围重叠，用于判断 ACPI 表是否需要备份（acpi.c）
// 要考虑到 RECLAIMABLE 内存
INIT_TEXT int rammap_hasoverlap(size_t addr, size_t len) {
    ASSERT(NULL != g_rammap);

    size_t end = addr + len;

    for (int i = 0; i < g_rammap_len; ++i) {
        ram_type_t type = g_rammap[i].type;
        if ((RAM_AVAILABLE != type) && (RAM_RECLAIMABLE != type)) {
            continue;
        }
        if ((end > g_rammap[i].addr) && (addr < g_rammap[i].end)) {
            return 1;
        }
    }

    return 0;
}

#ifdef DEBUG

static INIT_TEXT const char *ram_type_str(ram_type_t type) {
    switch (type) {
    case RAM_RESERVED: return "reserved";
    case RAM_AVAILABLE: return "available";
    case RAM_RECLAIMABLE: return "reclaimable";
    }
}

INIT_TEXT void rammap_show() {
    ASSERT(NULL != g_rammap);

    dbg_print("ram ranges:\n");
    for (int i = 0; i < g_rammap_len; ++i) {
        size_t addr = g_rammap[i].addr;
        size_t end  = g_rammap[i].end;
        const char *type = ram_type_str(g_rammap[i].type);
        dbg_print("  - ram range: addr=0x%016zx, end=0x%016zx, type=%s\n", addr, end, type);
    }
}

#endif // DEBUG
