#include <arch_mem.h>
#include <arch_cpu.h>
#include <arch_mmu.h>
#include <arch_smp.h>
#include <arch_api_p.h>

#include <wheel.h>
#include <vmspace.h>
#include <page.h>
#include <str.h>


// //------------------------------------------------------------------------------
// // 本机物理内存布局
// //------------------------------------------------------------------------------

// CONST pmrange_t *g_pmmap = NULL;
// CONST int g_pmmap_len = 0;


// // 检查一段内存是否与可用内存范围重叠，用于判断 ACPI 表是否需要备份（acpi.c）
// // 要考虑到 RECLAIMABLE 内存
// INIT_TEXT int pmmap_hasoverlap(size_t addr, size_t len) {
//     ASSERT(NULL != g_pmmap);
//     ASSERT(g_pmmap_len > 0);

//     size_t end = addr + len;

//     for (int i = 0; i < g_pmmap_len; ++i) {
//         pmtype_t type = g_pmmap[i].type;
//         if ((PM_AVAILABLE != type) && (PM_RECLAIMABLE != type)) {
//             continue;
//         }
//         if ((end > g_pmmap[i].addr) && (addr < g_pmmap[i].end)) {
//             return 1;
//         }
//     }

//     return 0;
// }

// #ifdef DEBUG

// static INIT_TEXT const char *pmtype_str(pmtype_t type) {
//     switch (type) {
//     case PM_RESERVED: return "reserved";
//     case PM_AVAILABLE: return "available";
//     case PM_RECLAIMABLE: return "reclaimable";
//     }
// }

// INIT_TEXT void pmmap_show() {
//     ASSERT(NULL != g_pmmap);
//     ASSERT(g_pmmap_len > 0);

//     klog("ram ranges:\n");
//     for (int i = 0; i < g_pmmap_len; ++i) {
//         size_t addr = g_pmmap[i].addr;
//         size_t end  = g_pmmap[i].end;
//         const char *type = pmtype_str(g_pmmap[i].type);
//         klog("  - ram range: addr=0x%016zx, end=0x%016zx, type=%s\n", addr, end, type);
//     }
// }

// #endif // DEBUG



// //------------------------------------------------------------------------------
// // 启动阶段内存分配，不释放
// //------------------------------------------------------------------------------

// typedef struct buff {
//     uint8_t *ptr;
//     uint8_t *end;
// } buff_t;

// static SECTION(".rotail") ALIGNED(16) uint8_t g_ro_area[EARLY_RO_SIZE];
// static SECTION(".rwtail") ALIGNED(16) uint8_t g_rw_area[EARLY_RW_SIZE];
// static INIT_BSS buff_t g_ro_buff;
// static INIT_BSS buff_t g_rw_buff;

// INIT_TEXT void early_alloc_init() {
//     g_ro_buff.ptr = g_ro_area;
//     g_ro_buff.end = g_ro_area + sizeof(g_ro_area);
//     g_rw_buff.ptr = g_rw_area;
//     g_rw_buff.end = g_rw_area + sizeof(g_rw_area);
// }

// static INIT_TEXT void *buff_grow(buff_t *buff, size_t size) {
//     if (buff->ptr + size >= buff->end) {
//         return NULL;
//     }
//     size +=  15UL;
//     size &= ~15UL;
//     uint8_t *p = buff->ptr;
//     buff->ptr += size;
//     return p;
// }

// INIT_TEXT void *early_alloc_ro(size_t size) {
//     void *p = buff_grow(&g_ro_buff, size);
//     if (NULL == p) {
//         klog("fatal: early ro alloc buffer overflow!\n");
//         return NULL;
//     }
//     return p;
// }

// INIT_TEXT void *early_alloc_rw(size_t size) {
//     void *p = buff_grow(&g_rw_buff, size);
//     if (NULL == p) {
//         klog("fatal: early rw alloc buffer overflow!\n");
//         return NULL;
//     }
//     return p;
// }

// // 将 early_rw 可分配范围延长到所在内存的上限
// // 此时 ACPI 内存尚未回收，只考虑 AVAILABLE 类型的内存范围
// INIT_TEXT void early_rw_unlock() {
//     size_t ptr = (size_t)g_rw_buff.ptr - KERNEL_TEXT_ADDR;

//     for (int i = 0; i < g_pmmap_len; ++i) {
//         if (PM_AVAILABLE != g_pmmap[i].type) {
//             continue;
//         }
//         size_t start = g_pmmap[i].addr;
//         size_t end = g_pmmap[i].end;
//         if ((start <= ptr) && (ptr < end)) {
//             g_rw_buff.end = (uint8_t *)end + KERNEL_TEXT_ADDR;
//             return;
//         }
//     }
// }



//------------------------------------------------------------------------------
// per-CPU 数据区
//------------------------------------------------------------------------------

// 这几个函数与 arch_smp.c 也有关系

extern char _pcpu_addr, _pcpu_data_end, _pcpu_bss_end;

static CONST size_t g_pcpu_size = 0;

// static CONST uint8_t **g_pcpu_areas = NULL;
static CONST size_t *g_pcpu_offsets = NULL;

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

// 内核虚拟地址空间和页表
static vmspace_t g_kernel_vm;
static uint64_t g_kernel_cr3 = INVALID_ADDR;

static INIT_BSS vmrange_t  g_range_init;     // 初始化代码数据，还有 PCPU 模板、实模式代码
static CONST    vmrange_t  g_range_text;
static CONST    vmrange_t  g_range_rodata;   // 结束位置由 g_ro_buff 决定
static CONST    vmrange_t  g_range_data;     // 结束位置由 g_rw_buff 决定

// 每个 PCPU 都需要专门的 range，需要分别描述静态 PCPU 和异常栈
static CONST    vmrange_t *g_range_pcpu_vars;
static CONST    vmrange_t *g_range_pcpu_nmi;
static CONST    vmrange_t *g_range_pcpu_df;
static CONST    vmrange_t *g_range_pcpu_mc;



// 将一段内存标记为内核占用，记录在地址空间里，也记录在物理内存管理器中
// 地址必须按页对齐，因为涉及到物理页管理
static INIT_TEXT void add_range(vmrange_t *rng, void *addr, void *end, const char *desc) {
    rng->addr = (size_t)addr;
    rng->end  = (size_t)end;
    rng->desc = desc;

    rng->addr &= ~(PAGE_SIZE - 1);
    rng->end  +=   PAGE_SIZE - 1;
    rng->end  &= ~(PAGE_SIZE - 1);

    // klog("adding kernel vm range %zx~%zx, %s\n", rng->addr, rng->end, desc);
    vmspace_add(&g_kernel_vm, rng);
    page_add(rng->addr - KERNEL_TEXT_ADDR, rng->end - KERNEL_TEXT_ADDR, PT_KERNEL);
}


// 划分内存，停用 early-alloc，启用物理页分配
INIT_TEXT void mem_init() {
    // ASSERT(NULL != g_pmmap);
    // ASSERT(g_pmmap_len > 0);

    int ncpu = cpu_count();
    ASSERT(ncpu > 0);

    // 提前划分 pcpu 指针
    g_pcpu_offsets = early_alloc_ro(ncpu * sizeof(size_t));
    g_range_pcpu_vars = early_alloc_ro(ncpu * sizeof(vmrange_t));

    // 分配页描述符（page 数组不一定要处于 data 末尾）
    size_t ramtop = 0;
    for (int i = g_pmmap_len - 1; i >= 0; --i) {
        pmtype_t type = g_pmmap[i].type;
        if ((PM_AVAILABLE != type) && (PM_RECLAIMABLE != type)) {
            continue;
        }
        ramtop = g_pmmap[i].end & ~(PAGE_SIZE - 1);
        break;
    }
    page_init(ramtop);

    // 准备

    // 禁用临时内存分配
    early_alloc_disable();

    // 记录内核的地址空间布局，也是后面建立页表的依据
    vmspace_init(&g_kernel_vm);
    void *kernel_addr = (void *)(KERNEL_TEXT_ADDR + KERNEL_LOAD_ADDR);
    add_range(&g_range_init, kernel_addr, &_init_end, "init");
    add_range(&g_range_text, &_text_addr, &_text_end, "text");
    add_range(&g_range_rodata, &_rodata_addr, early_alloc_ro(0), "rodata");
    add_range(&g_range_data, &_data_addr, early_alloc_rw(0), "data");

    // 获取 L1 大小（一路）
    size_t l1size = g_l1d_info.line_size * g_l1d_info.sets;
    if (0 == l1size) {
        l1size = PAGE_SIZE;
    }
    ASSERT(0 == (l1size & (l1size - 1)));

    // 计算 PCPU 大小，顺便划分
    size_t pcpu_copy = (size_t)(&_pcpu_data_end - &_pcpu_addr);
    size_t pcpu_size = (size_t)(&_pcpu_bss_end  - &_pcpu_addr);
    size_t pcpu_skip = pcpu_size + PAGE_SIZE; // 每个 PCPU 前面留有 guard page

    // 计算 PCPU 大小，包括变量和异常栈，之间还留有 guard page
    size_t pcpu_vars_size = (size_t)(&_pcpu_bss_end  - &_pcpu_addr);
    size_t pcpu_nmi_offset = (pcpu_vars_size + 2 * PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);


    g_pcpu_size = (size_t)(&_pcpu_bss_end  - &_pcpu_addr);
    g_pcpu_size = (g_pcpu_size + 2 * PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    size_t nmi_offset = g_pcpu_size + PAGE_SIZE;
    g_pcpu_size += PAGE_SIZE + INT_STACK_SIZE;  // NMI stack (with guard)
    size_t df_offset = g_pcpu_size + PAGE_SIZE;
    g_pcpu_size += PAGE_SIZE + INT_STACK_SIZE;  // #DF stack (with guard)
    size_t mc_offset = g_pcpu_size + PAGE_SIZE;
    g_pcpu_size += PAGE_SIZE + INT_STACK_SIZE;  // #MC stack (with guard)

    // 需要对齐到 L1
    g_pcpu_size  = (g_pcpu_size + l1size - 1) & ~(l1size - 1);

    // PCPU 对齐到 L1
    pcpu_skip +=   l1size - 1;
    pcpu_skip &= ~(l1size - 1);
    size_t rw_end = ((size_t)early_alloc_rw(0) + PAGE_SIZE + l1size - 1) & ~(l1size - 1);

    // PCPU 的起始地址只需要对齐到页，不用对齐到 L1
    size_t kernelend = ((size_t)early_alloc_rw(0) + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);

    // 划分每个 PCPU 的空间
    for (int i = 0; i < cpu_count(); ++i) {
        bcpy((uint8_t *)rw_end, &_pcpu_addr, pcpu_copy);
        add_range(&g_range_pcpu_vars[i], (uint8_t *)rw_end, (uint8_t *)(rw_end + pcpu_size), "pcpu");
        g_pcpu_offsets[i] = rw_end - (size_t)&_pcpu_addr;
        rw_end += pcpu_skip;
    }
    rw_end -= PAGE_SIZE; // 最后一个 guard page 不需要存在

    // 显示内核地址空间布局
    vmspace_show(&g_kernel_vm);

    // 遍历物理内存范围，将可用内存添加给页分配器
    // RECLAIMABLE 也属于可用范围，跳过 1M 以下的 lowmem 与内核占用的部分
    rw_end -= KERNEL_TEXT_ADDR;
    for (int i = 0; i < g_pmmap_len; ++i) {
        pmtype_t type = g_pmmap[i].type;
        if ((PM_AVAILABLE != type) && (PM_RECLAIMABLE != type)) {
            continue;
        }

        size_t start = g_pmmap[i].addr;
        size_t end = g_pmmap[i].end;
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

    // 相邻的 vmrange 之间还有空隙，虚拟地址保留作为 guard page，但是物理页可以回收
    for (dlnode_t *i = g_kernel_vm.head.next->next; i != &g_kernel_vm.head; i = i->next) {
        vmrange_t *prev = containerof(i->prev, vmrange_t, dl);
        vmrange_t *curr = containerof(i, vmrange_t, dl);
        page_add(prev->end, curr->addr, PT_FREE);
    }
}


static void map_range(const vmrange_t *rng, mmu_attr_t attrs) {
    klog("  >> mapping %zx~%zx\n", rng->addr, rng->end);
    mmu_map(g_kernel_cr3, rng->addr, rng->end, rng->addr - KERNEL_TEXT_ADDR, attrs);
}


// 创建内核页表
INIT_TEXT void ctx_init() {
    ASSERT(INVALID_ADDR == g_kernel_cr3);

    g_kernel_cr3 = mmu_table_create();

    map_range(&g_range_init, MMU_WRITE|MMU_EXEC);
    map_range(&g_range_text, MMU_EXEC);
    map_range(&g_range_rodata, 0);
    map_range(&g_range_data, MMU_WRITE);

    // 遍历剩下的 pcpu 和异常栈
    for (dlnode_t *i = g_range_data.dl.next; i != &g_kernel_vm.head; i = i->next) {
        map_range(containerof(i, vmrange_t, dl), MMU_WRITE);
    }
    // for (int i = 0; i < cpu_count(); ++i) {
    //     map_range(&g_range_pcpu_vars[i], MMU_WRITE);
    // }

    // 将所有物理内存映射到内核地址空间
    // 至少映射 4GB，因为涉及到 APIC、framebuf 等硬件设备
    // 物理内存布局中，不仅会列出 ram，还包硬件映射
    uint64_t maplen = g_pmmap[g_pmmap_len - 1].end;
    mmu_map(g_kernel_cr3, DIRECT_MAP_ADDR, DIRECT_MAP_ADDR + maplen, 0, MMU_WRITE);

    write_cr3(g_kernel_cr3);
}


// 回收 init 部分的内存空间（本函数不能使用 init 函数）
void reclaim_init() {
    // TODO 同时操作 vmspace 和 page-alloc
    //      移除地址空间中的 vmrange，并将这段物理内存整体回收
}
