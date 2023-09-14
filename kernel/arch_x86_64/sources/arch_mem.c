// 物理内存范围管理、地址空间管理


#include <arch_mem.h>
#include <arch_cpu.h>
#include <debug.h>
#include <strlib.h>
#include <page.h>
#include <vmspace.h>


//------------------------------------------------------------------------------
// 启动阶段的内存分配
//------------------------------------------------------------------------------

typedef struct membuff {
    uint8_t *ptr;
    size_t size;
    size_t used;
} membuff_t;

#define BUFF_ALIGN 16

static SECTION(".rotail") ALIGNED(BUFF_ALIGN) uint8_t g_ro_area[EARLY_RO_BUFF_SIZE];
static SECTION(".rwtail") ALIGNED(BUFF_ALIGN) uint8_t g_rw_area[EARLY_RW_BUFF_SIZE];
static INIT_DATA membuff_t g_ro_buff = { g_ro_area, EARLY_RO_BUFF_SIZE, 0 };
static INIT_DATA membuff_t g_rw_buff = { g_rw_area, EARLY_RW_BUFF_SIZE, 0 };

static INIT_TEXT void *membuff_grow(membuff_t *buff, size_t size) {
    if (buff->used + size >= buff->size) {
        return NULL;
    }
    size +=   BUFF_ALIGN - 1;
    size &= ~(BUFF_ALIGN - 1);
    uint8_t *p = &buff->ptr[buff->used];
    buff->used += size;
    return p;
}

INIT_TEXT void *early_alloc_ro(size_t size) {
    void *p = membuff_grow(&g_ro_buff, size);
    if (NULL == p) {
        klog("early ro alloc buffer overflow!\n");
        cpu_halt();
    }
    return p;
}

INIT_TEXT void *early_alloc_rw(size_t size) {
    void *p = membuff_grow(&g_rw_buff, size);
    if (NULL == p) {
        klog("early rw alloc buffer overflow!\n");
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

static INIT_TEXT void early_alloc_disable() {
    g_ro_buff.size = 0;
    g_rw_buff.size = 0;
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
    mfill(g_rammap, 0, num * sizeof(ram_range_t));
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

    klog("error: address %lx not in valid ram range\n", addr);
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

    klog("ram ranges:\n");
    for (int i = 0; i < g_rammap_len; ++i) {
        size_t addr = g_rammap[i].addr;
        size_t end  = g_rammap[i].end;
        const char *type = ram_type_str(g_rammap[i].type);
        klog("  - ram range: addr=0x%016zx, end=0x%016zx, type=%s\n", addr, end, type);
    }
}

#endif // DEBUG


//------------------------------------------------------------------------------
// 划分内存区域
//------------------------------------------------------------------------------


// layout.ld
extern char _pcpu_addr, _pcpu_data_end, _pcpu_bss_end;
extern char _init_end;
extern char _text_addr, _text_end;
extern char _rodata_addr;
extern char _data_addr;

// arch_smp.c
extern size_t *g_pcpu_offsets;

// 这些都是临时内存范围
static INIT_BSS vmrange_t  g_range_init;    // 初始化代码数据，还有 PCPU 模板、实模式代码
static          vmrange_t  g_range_text;
static          vmrange_t  g_range_rodata;  // 结束位置由 early_ro_buff 决定
static          vmrange_t  g_range_data;    // 结束位置由 early_rw_buff 决定
static          vmrange_t *g_range_pcpu;    // 每个 PCPU 都需要专门的 range

// 记录内核虚拟地址空间布局
static vmspace_t g_kernel_vm;


// 标记一段内存范围
static INIT_TEXT void add_kernel_range(vmrange_t *rng, void *addr, void *end, const char *desc) {
    rng->addr = (size_t)addr;
    rng->size = (size_t)end - (size_t)addr;
    rng->desc = desc;
    vmspace_insert_range(&g_kernel_vm, rng);
}


INIT_TEXT void mem_init() {
    ASSERT(g_rammap_len > 0); // 需要知道物理内存分布
    ASSERT(cpu_count() > 0);  // 需要知道 CPU 个数

    // 统计可用内存上限，即需要管理的页范围
    size_t ramend = 0;
    for (int i = 0; i < g_rammap_len; ++i) {
        ram_type_t type = g_rammap[i].type;
        if ((RAM_AVAILABLE != type) && (RAM_RECLAIMABLE != type)) {
            continue;
        }
        if (ramend < g_rammap[i].end) {
            ramend = g_rammap[i].end;
        }
    }

    // 准备 PCPU 偏移量数组
    g_pcpu_offsets = early_alloc_ro(cpu_count() * sizeof(size_t));
    g_range_pcpu = early_alloc_rw(cpu_count() * sizeof(vmrange_t));

    // 分配页描述符，初始化页分配器（这一步仍需要 early_alloc）
    pages_init(0, ramend);

    // 记录当前数据段结束位置，并禁用 early_alloc
    uint8_t *ro_end = early_alloc_ro(0);
    uint8_t *rw_end = early_alloc_rw(0);
    early_alloc_disable();

    // 内核虚拟地址空间
    vmspace_init(&g_kernel_vm);

    // 将几个内核 section 记录在地址空间中
    add_kernel_range(&g_range_init, &_pcpu_addr, &_init_end, "init");
    add_kernel_range(&g_range_text, &_text_addr, &_text_end, "text");
    add_kernel_range(&g_range_rodata, &_rodata_addr, ro_end, "rodata");
    add_kernel_range(&g_range_data, &_data_addr, rw_end, "data");

    // 获取 L1 一行大小，一路大小
    size_t l1_line = g_l1d_info.line_size;
    size_t l1_size = l1_line * g_l1d_info.sets;
    ASSERT(0 == (l1_line & (l1_line - 1)));
    ASSERT(0 == (l1_size & (l1_size - 1)));

    // 划分 PCPU 区域，地址按缓存行对齐，总大小按缓存大小对齐
    // 确保不同 CPU 访问各自的 PCPU 变量时，映射到相同的 set
    // 只需考虑到 L1，因为 L2、L3 缓存是所有 CPU 共享的
    size_t pcpu_size = (size_t)(&_pcpu_bss_end  - &_pcpu_addr);
    size_t pcpu_copy = (size_t)(&_pcpu_data_end - &_pcpu_addr);
    rw_end = (uint8_t *)(((size_t)rw_end + l1_line - 1) & ~(l1_line - 1));
    pcpu_size = (pcpu_size + l1_size - 1) & ~(l1_size - 1);
    for (int i = 0; i < cpu_count(); ++i) {
        g_pcpu_offsets[i] = (size_t)rw_end - (size_t)&_pcpu_addr;
        mcopy(rw_end, &_pcpu_addr, pcpu_copy);
        mfill(rw_end + pcpu_copy, 0, pcpu_size - pcpu_copy);
        add_kernel_range(&g_range_pcpu[i], rw_end, rw_end + pcpu_size, "pcpu");
        rw_end += pcpu_size;
    }

    // 获取内核占用的物理页范围，包括 boot、init
    size_t kstart = KERNEL_LOAD_ADDR >> PAGE_SHIFT;
    size_t kend = ((size_t)rw_end - KERNEL_TEXT_BASE + PAGE_SIZE - 1) >> PAGE_SHIFT;

    // 遍历物理内存范围，将可用内存添加给页分配器
    // 包括 ACPI 部分，跳过 1M 以下与内核占用的部分
    for (int i = 0; i < g_rammap_len; ++i) {
        ram_type_t type = g_rammap[i].type;
        if ((RAM_AVAILABLE != type) && (RAM_RECLAIMABLE != type)) {
            continue;
        }

        size_t start = (g_rammap[i].addr + PAGE_SIZE - 1) >> PAGE_SHIFT;
        size_t end = g_rammap[i].end >> PAGE_SHIFT;
        if (start < 0x100) {
            start = 0x100;
        }
        if (start >= end) {
            continue;
        }

        // 内核占据的内存必然属于一个 range
        if ((start <= kstart) && (end >= kend)) {
            pages_add(start, kstart);
            pages_add(kend, end);
        } else {
            pages_add(start, end);
        }
    }
}

// 回收启动阶段占用的内存，交给页分配器
void reclaim_init() {
    size_t init_start = KERNEL_LOAD_ADDR >> PAGE_SHIFT;
    size_t init_end = ((size_t)&_init_end - KERNEL_TEXT_BASE + PAGE_SIZE - 1) >> PAGE_SHIFT;

    pages_add(init_start, init_end);
}
