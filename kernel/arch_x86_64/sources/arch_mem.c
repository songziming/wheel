#include <arch_mem.h>
#include <wheel.h>


//------------------------------------------------------------------------------
// 本机物理内存布局
//------------------------------------------------------------------------------

CONST size_t g_rammap_len = 0;
CONST ram_range_t *g_rammap = NULL;

// 返回 addr 所在的内存范围的截止地址，用于确定 early_alloc_buff 的增长极限
// 此时 ACPI 内存尚未回收，只考虑 AVAILABLE 类型的内存范围
INIT_TEXT size_t rammap_extentof(size_t addr) {
    ASSERT(NULL != g_rammap);

    for (size_t i = 0; i < g_rammap_len; ++i) {
        if (RAM_AVAILABLE != g_rammap[i].type) {
            continue;
        }
        size_t start = g_rammap[i].addr;
        size_t end = g_rammap[i].end;
        if ((start <= addr) && (addr < end)) {
            return end;
        }
    }

    klog("error: address %lx not in valid ram range\n", addr);
    return 0;
}

// 检查一段内存是否与可用内存范围重叠，用于判断 ACPI 表是否需要备份（acpi.c）
// 要考虑到 RECLAIMABLE 内存
INIT_TEXT int rammap_hasoverlap(size_t addr, size_t len) {
    ASSERT(NULL != g_rammap);

    size_t end = addr + len;

    for (size_t i = 0; i < g_rammap_len; ++i) {
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

    klog("ram ranges:\n");
    for (size_t i = 0; i < g_rammap_len; ++i) {
        size_t addr = g_rammap[i].addr;
        size_t end  = g_rammap[i].end;
        const char *type = ram_type_str(g_rammap[i].type);
        klog("  - ram range: addr=0x%016zx, end=0x%016zx, type=%s\n", addr, end, type);
    }
}

#endif // DEBUG
