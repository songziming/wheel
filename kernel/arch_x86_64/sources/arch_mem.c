#include <arch_mem.h>
#include <arch_cpu.h>
#include <arch_smp.h>
#include <arch_api_p.h>

#include <wheel.h>
#include <vmspace.h>
#include <page.h>
#include <str.h>


//------------------------------------------------------------------------------
// 本机物理内存布局
//------------------------------------------------------------------------------

CONST int g_rammap_len = 0;
CONST ram_range_t *g_rammap = NULL;

// 获取本机物理内存上限
static INIT_TEXT size_t rammap_top() {
    ASSERT(0 != g_rammap_len);
    ASSERT(NULL != g_rammap);

    for (int i = g_rammap_len - 1; i >= 0; --i) {
        ram_type_t type = g_rammap[i].type;
        if ((RAM_AVAILABLE != type) && (RAM_RECLAIMABLE != type)) {
            continue;
        }
        return g_rammap[i].end;
    }

    return 0;
}

// 返回 addr 所在的内存范围的截止地址，用于确定 early_alloc_buff 的增长极限
// 此时 ACPI 内存尚未回收，只考虑 AVAILABLE 类型的内存范围
static INIT_TEXT size_t rammap_extentof(size_t addr) {
    ASSERT(0 != g_rammap_len);
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

    klog("warning: address %lx not in valid ram range\n", addr);
    return 0;
}

// 检查一段内存是否与可用内存范围重叠，用于判断 ACPI 表是否需要备份（acpi.c）
// 要考虑到 RECLAIMABLE 内存
INIT_TEXT int rammap_hasoverlap(size_t addr, size_t len) {
    ASSERT(0 != g_rammap_len);
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
    ASSERT(0 != g_rammap_len);
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
// 启动阶段内存分配，不释放
//------------------------------------------------------------------------------

typedef struct buff {
    uint8_t *ptr;
    uint8_t *end;
} buff_t;

static SECTION(".rotail") ALIGNED(16) uint8_t g_ro_area[EARLY_RO_SIZE];
static SECTION(".rwtail") ALIGNED(16) uint8_t g_rw_area[EARLY_RW_SIZE];
static INIT_BSS buff_t g_ro_buff;
static INIT_BSS buff_t g_rw_buff;

INIT_TEXT void early_alloc_init() {
    g_ro_buff.ptr = g_ro_area;
    g_ro_buff.end = g_ro_area + sizeof(g_ro_area);
    g_rw_buff.ptr = g_rw_area;
    g_rw_buff.end = g_rw_area + sizeof(g_rw_area);
}

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
    g_rw_buff.end = (uint8_t *)end;
}



//------------------------------------------------------------------------------
// per-CPU 数据区
//------------------------------------------------------------------------------

// 这几个函数与 arch_smp.c 也有关系

extern char _pcpu_addr, _pcpu_data_end, _pcpu_bss_end;

static CONST uint8_t **g_pcpu_areas = NULL;
static CONST size_t   *g_pcpu_offsets = NULL;

static PCPU_BSS int g_cpu_index; // 每个 CPU 的编号

void *pcpu_ptr(int idx, void *ptr) {
    ASSERT(NULL != g_pcpu_offsets);
    ASSERT(idx < cpu_count());
    ASSERT((char *)ptr >= &_pcpu_addr);
    ASSERT((char *)ptr < &_pcpu_bss_end);
    return (void *)(g_pcpu_offsets[idx] + (size_t)ptr);
}

// 设置 this-cpu 指针，并且设置 CPU 编号
INIT_TEXT void gsbase_init(int idx) {
    ASSERT(NULL != g_pcpu_offsets);
    ASSERT(idx < cpu_count());

    write_gsbase(g_pcpu_offsets[idx]);
    __asm__("movl %0, %%gs:%1" :: "r"(idx), "m"(g_cpu_index));
}

// 依赖 gsbase
inline int cpu_index() {
    ASSERT(NULL != g_pcpu_offsets);
    int idx;
    __asm__("movl %%gs:%1, %0" : "=a"(idx) : "m"(g_cpu_index));
    return idx;
}

// 依赖 gsbase
inline void *this_ptr(void *ptr) {
    ASSERT(NULL != g_pcpu_offsets);
    ASSERT((char *)ptr >= &_pcpu_addr);
    ASSERT((char *)ptr < &_pcpu_bss_end);
    return (uint8_t *)ptr + read_gsbase();
}



//------------------------------------------------------------------------------
// 划分内存布局
//------------------------------------------------------------------------------

// layout.ld
extern char _init_end;
extern char _text_addr, _text_end;
extern char _rodata_addr;
extern char _data_addr;

// 内核虚拟地址空间布局
static vmspace_t g_kernel_vm;

static INIT_BSS vmrange_t g_range_init;     // 初始化代码数据，还有 PCPU 模板、实模式代码
static CONST    vmrange_t g_range_text;
static CONST    vmrange_t g_range_rodata;   // 结束位置由 g_ro_buff 决定
static CONST    vmrange_t g_range_data;     // 结束位置由 g_rw_buff 决定
static PCPU_BSS vmrange_t g_range_pcpu;     // 每个 PCPU 都需要专门的 range


// 将一段内存标记为内核占用，记录在地址空间里，也记录在物理内存管理器中
// 地址必须按页对齐，因为涉及到物理页管理
static INIT_TEXT void add_range(vmrange_t *rng, void *addr, void *end, const char *desc) {
    rng->addr = (size_t)addr;
    rng->size = (size_t)end - (size_t)addr;
    rng->desc = desc;
    vmspace_add(&g_kernel_vm, rng);
    page_add((size_t)addr - KERNEL_TEXT_ADDR, (size_t)end - KERNEL_TEXT_ADDR, PT_KERNEL);
}


// 划分内存，停用 early-alloc，启用物理页分配
INIT_TEXT void mem_init() {
    ASSERT(g_rammap_len > 0); // 需要知道物理内存分布
    ASSERT(cpu_count() > 0);  // 需要知道 CPU 个数

    // 提前划分 pcpu 指针
    g_pcpu_areas = early_alloc_ro(cpu_count() * sizeof(uint8_t *));
    g_pcpu_offsets = early_alloc_ro(cpu_count() * sizeof(size_t));

    // 分配页描述符
    page_init(rammap_top());

    // 禁用临时内存分配
    g_ro_buff.end = g_ro_buff.ptr;
    g_rw_buff.end = g_rw_buff.ptr;

    // 记录内核的地址空间布局，也是后面建立页表的依据
    vmspace_init(&g_kernel_vm);
    void *kernel_addr = (void *)(KERNEL_TEXT_ADDR + KERNEL_LOAD_ADDR);
    add_range(&g_range_init, kernel_addr, &_init_end, "init");
    add_range(&g_range_text, &_text_addr, &_text_end, "text");
    add_range(&g_range_rodata, &_rodata_addr, g_ro_buff.end, "rodata");
    add_range(&g_range_data, &_data_addr, g_rw_buff.end, "data");

    // 获取 L1 一行大小，一路大小
    size_t l1_line = g_l1d_info.line_size;
    size_t l1_size = l1_line * g_l1d_info.sets;
    ASSERT(0 == (l1_line & (l1_line - 1)));
    ASSERT(0 == (l1_size & (l1_size - 1)));

    // PCPU 大小按 L1 一路对齐，PCPU 地址按 L1 一行对齐
    size_t pcpu_copy = (size_t)(&_pcpu_data_end - &_pcpu_addr);
    size_t pcpu_size = (size_t)(&_pcpu_bss_end  - &_pcpu_addr);
    size_t pcpu_skip = (pcpu_size + l1_size - 1) & ~(l1_size - 1);
    size_t rw_end = ((size_t)g_rw_buff.end + l1_line - 1) & ~(l1_line - 1);

    // 划分每个 PCPU 的空间
    // PCPU 和 kernel-data 是连在一起的，不算两个 section
    for (int i = 0; i < cpu_count(); ++i) {
        g_pcpu_areas[i] = (uint8_t *)rw_end;
        g_pcpu_offsets[i] = rw_end - (size_t)(&_pcpu_addr);
        bcpy(g_pcpu_areas[i], &_pcpu_addr, pcpu_copy);
        rw_end += pcpu_skip;

        vmrange_t *rng = pcpu_ptr(i, &g_range_pcpu);
        add_range(rng, g_pcpu_areas[i], g_pcpu_areas[i] + pcpu_size, "pcpu");
    }

    vmspace_show(&g_kernel_vm);

    // 遍历物理内存范围，将可用内存添加给页分配器
    // 包括 ACPI 部分，跳过 1M 以下与内核占用的部分
    rw_end -= KERNEL_TEXT_ADDR;
    for (int i = 0; i < g_rammap_len; ++i) {
        ram_type_t type = g_rammap[i].type;
        if ((RAM_AVAILABLE != type) && (RAM_RECLAIMABLE != type)) {
            continue;
        }

        size_t start = g_rammap[i].addr;
        size_t end = g_rammap[i].end;
        if (start < 0x100000) {
            start = 0x100000;
        }
        if (start >= end) {
            continue;
        }

        // 内核占据的内存范围必然完整包含于一个 range
        if ((start <= KERNEL_LOAD_ADDR) && (end >= rw_end)) {
            page_add(start, KERNEL_LOAD_ADDR, PT_FREE);
            page_add(rw_end, end, PT_FREE);
        } else {
            page_add(start, end, PT_FREE);
        }
    }
}


// 回收 init 部分的内存空间（本函数不能使用 init 函数）
void reclaim_init() {
    // TODO 同时操作 vmspace 和 page-alloc
    //      移除地址空间中的 vmrange，并将这段物理内存整体回收
}
