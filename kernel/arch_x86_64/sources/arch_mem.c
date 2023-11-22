#include <arch_mem.h>
#include <arch_cpu.h>
#include <arch_smp.h>

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

    klog("warning: address %lx not in valid ram range\n", addr);
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



//------------------------------------------------------------------------------
// 启动阶段内存分配，不释放
//------------------------------------------------------------------------------

typedef struct buff {
    uint8_t *ptr;
    uint8_t *end;
} buff_t;

static SECTION(".rotail") ALIGNED(16) uint8_t g_ro_area[EARLY_RO_SIZE];
static SECTION(".rwtail") ALIGNED(16) uint8_t g_rw_area[EARLY_RW_SIZE];
static INIT_DATA buff_t g_ro_buff = { g_ro_area, g_ro_area + EARLY_RO_SIZE };
static INIT_DATA buff_t g_rw_buff = { g_rw_area, g_rw_area + EARLY_RW_SIZE };

static INIT_TEXT void *buff_grow(buff_t *buff, size_t size) {
    if (buff->ptr + size >= buff->end) {
        return NULL;
    }
    size +=  15UL;
    size &= ~15UL;
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

INIT_TEXT void early_rw_unlock() {
    size_t ptr = (size_t)g_rw_buff.ptr - KERNEL_TEXT_ADDR;
    size_t end = rammap_extentof(ptr) + KERNEL_TEXT_ADDR;
    klog("extending early buff from %p to %zx\n", g_rw_buff.end, end);
    g_rw_buff.end = (uint8_t *)end;
}



//------------------------------------------------------------------------------
// per-CPU 数据区
//------------------------------------------------------------------------------

// static CONST void **g_pcpu_areas
static CONST size_t *g_pcpu_offsets = NULL;

// layout.ld
extern char _pcpu_addr;
extern char _pcpu_data_end;
extern char _pcpu_bss_end;

INIT_TEXT void pcpu_init() {
    ASSERT(NULL == g_pcpu_offsets);
    ASSERT(0 != g_loapic_num);

    // 获取 L1 一行大小，一路大小
    size_t l1_line = g_l1d_info.line_size;
    size_t l1_size = l1_line * g_l1d_info.sets;
    ASSERT(0 == (l1_line & (l1_line - 1)));
    ASSERT(0 == (l1_size & (l1_size - 1)));

    size_t pcpu_size = (size_t)(&_pcpu_bss_end  - &_pcpu_addr);
    size_t pcpu_copy = (size_t)(&_pcpu_data_end - &_pcpu_addr);
    klog("pcpu size %zu %zu\n", pcpu_size, pcpu_copy);
}



//------------------------------------------------------------------------------
// 划分内存布局
//------------------------------------------------------------------------------

INIT_TEXT void mem_init() {
    ASSERT(g_rammap_len > 0); // 需要知道物理内存分布
    ASSERT(cpu_count() > 0);  // 需要知道 CPU 个数

    // 统计可用内存上限
    size_t ramtop = 0;
    for (size_t i = 0; i < g_rammap_len; ++i) {
        ram_type_t type = g_rammap[i].type;
        if ((RAM_AVAILABLE != type) && (RAM_RECLAIMABLE != type)) {
            continue;
        }
        if (ramtop < g_rammap[i].end) {
            ramtop = g_rammap[i].end;
        }
    }

    klog("memory limit %zx\n", ramtop);
}
