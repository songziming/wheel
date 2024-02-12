// 物理内存管理、地址空间管理

#include <arch_mem.h>
#include <arch_smp.h>
#include <arch_interface.h>
#include <arch_debug.h>
#include <liba/cpuid.h>
// #include <liba/mmu.h>
#include <liba/rw.h>

#include <debug.h>
#include <page.h>
#include <early_heap.h>
#include <libk.h>
#include <dllist.h>

//------------------------------------------------------------------------------
// 启动阶段的临时内存分配
//------------------------------------------------------------------------------

static SECTION(".rotail") uint8_t g_rotail_buff[EARLY_CONST_HEAP_SIZE];
static SECTION(".tail") uint8_t g_tail_buff[EARLY_HEAP_SIZE];
static INIT_DATA early_heap_t g_early_const_heap;
static INIT_DATA early_heap_t g_early_heap;

INIT_TEXT void early_alloc_init() {
    early_heap_init(&g_early_const_heap, g_rotail_buff, &g_rotail_buff[EARLY_CONST_HEAP_SIZE]);
    early_heap_init(&g_early_heap, g_tail_buff, &g_tail_buff[EARLY_HEAP_SIZE]);
}

INIT_TEXT void *early_alloc(size_t size) {
    void *p = early_heap_alloc(&g_early_heap, size);
    if (NULL == p) {
        dbg_print("fatal: cannot early alloc data! req=%lx, rest=%lx\n",
            size, (size_t)(g_early_heap.end - g_early_heap.ptr));
        ASSERT(0);
        cpu_halt();
    }
    return p;
}

// 分配只读数据，初始化阶段可写，切换内核页表并开启写保护之后变为只读
INIT_TEXT void *early_const_alloc(size_t size) {
    void *p = early_heap_alloc(&g_early_const_heap, size);
    if (NULL == p) {
        dbg_print("warning: const buffer overflow!\n");
        p = early_heap_alloc(&g_early_heap, size);
    }
    if (NULL == p) {
        dbg_print("fatal: cannot early alloc const data!\n");
        cpu_halt();
    }
    return p;
}

INIT_TEXT void kernel_end_unlock() {
    size_t buff_pa = (size_t)g_tail_buff - KERNEL_TEXT_BASE;
    size_t ram_end = ram_range_get_limit(buff_pa) + KERNEL_TEXT_BASE;
    early_heap_set_end(&g_early_heap, (void *)ram_end);
}

//------------------------------------------------------------------------------
// 记录物理内存范围管理
//------------------------------------------------------------------------------

// 代表一段物理内存范围，左闭右开
typedef struct ram_range {
    size_t start;
    size_t end;
} ram_range_t;

// 记录本机可用的物理地址范围，可能不是页对齐的
static CONST int g_range_count = 0;
static CONST ram_range_t *g_ranges = NULL;

// 预留一定数量的 ram_range 空间
INIT_TEXT void ram_range_reserve(int num) {
    ASSERT(NULL == g_ranges);

    g_range_count = num;
    g_ranges = early_const_alloc(num * sizeof(ram_range_t));
    memset(g_ranges, 0, num * sizeof(ram_range_t));
}

// 添加一段物理内存
INIT_TEXT void ram_range_set(int idx, size_t addr, size_t len) {
    ASSERT(NULL != g_ranges);
    ASSERT(idx >= 0);
    ASSERT(idx < g_range_count);

    g_ranges[idx].start = addr;
    g_ranges[idx].end   = addr + len;
}

// 检查一段内存是否与可用内存范围重叠
// 用于判断数据是否需要备份
INIT_TEXT int ram_range_has_overlap(size_t addr, size_t len) {
    ASSERT(NULL != g_ranges);

    size_t end = addr + len;

    for (int i = 0; i < g_range_count; ++i) {
        ASSERT(g_ranges[i].start < g_ranges[i].end);
        if ((end > g_ranges[i].start) && (addr < g_ranges[i].end)) {
            return 1;
        }
    }

    return 0;
}

// 返回 addr 之后能连续访问到的最高地址
INIT_TEXT size_t ram_range_get_limit(size_t addr) {
    ASSERT(NULL != g_ranges);

    for (int i = 0; i < g_range_count; ++i) {
        ram_range_t *rng = &g_ranges[i];
        if ((rng->start <= addr) && (addr < rng->end)) {
            return rng->end;
        }
    }

    dbg_print("address %lx not in valid ram range\n", addr);
    return 0;
}


//------------------------------------------------------------------------------
// 准备物理页面分配
//------------------------------------------------------------------------------

// 定义在 layout.ld
extern uint8_t _pcpu_addr;
extern uint8_t _pcpu_data_end;
extern uint8_t _pcpu_bss_end;
extern uint8_t _init_end;
extern uint8_t _text_addr;
extern uint8_t _text_end;
extern uint8_t _rodata_addr;
extern uint8_t _data_addr;

vmspace_t g_kernel_vm;      // 内核虚拟地址空间布局
size_t g_kernel_map = 0;    // 内核页表，被所有进程共享

static INIT_DATA vmrange_t  g_rng_k_init;
static CONST     vmrange_t  g_rng_k_text;
static CONST     vmrange_t  g_rng_k_rodata;
static CONST     vmrange_t  g_rng_k_data;
static CONST     vmrange_t *g_rng_k_pcpu;
extern CONST     size_t    *g_pcpu_offsets;  // arch_smp.c

static INIT_TEXT void add_kernel_range(vmrange_t *rng, uint8_t *addr, uint8_t *end, const char *desc) {
    rng->va   = ((size_t)addr + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    rng->size = ((size_t)end - rng->va + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    rng->desc = desc;
    vmspace_insert_range(&g_kernel_vm, rng);
}

static INIT_TEXT void map_kernel_range(vmrange_t *rng, uint32_t flags) {
    size_t pa = rng->va - KERNEL_TEXT_BASE;
    size_t npages = rng->size >> PAGE_SHIFT;
    mmu_map(g_kernel_map, rng->va, pa, npages, flags);
}

// 划分内存布局，为 per cpu 分配空间
// 设置物理页面分配器，建立内核页表
void mem_init() {
    ASSERT(g_range_count > 0);  // 需要知道物理内存布局
    ASSERT(g_loapic_count > 0); // 需要知道 CPU 数量

    // 获取总页数、L1 缓存大小
    size_t npages = g_ranges[g_range_count - 1].end >> PAGE_SHIFT;
    size_t l1_size = g_l1d_info.sets * g_l1d_info.line_size;
    if (0 == l1_size) {
        l1_size = PAGE_SIZE;
    }

    // 申请 per cpu 区域的 range
    g_rng_k_pcpu = early_const_alloc(g_loapic_count * sizeof(vmrange_t));
    g_pcpu_offsets = early_const_alloc(g_loapic_count * sizeof(size_t));

    // 根据物理内存大小准备页描述符（这一步会调用 early_alloc）
    pages_init(npages);

    // TODO 应该用 kernel_context 记录内核的地址空间，带有自旋锁保护
    //      初始化阶段，先把地址范围记录在 vmrange 内部，
    //      划分内存之后，再把 vmrange 注册到 kernel_ctx，顺便映射页表

    // 记录内核地址空间布局
    uint8_t *init_addr = (uint8_t *)(KERNEL_TEXT_BASE + KERNEL_LOAD_ADDR);
    uint8_t *rodata_end = (uint8_t *)early_heap_get_ptr(&g_early_const_heap);
    uint8_t *data_end = (uint8_t *)early_heap_get_ptr(&g_early_heap);
    vmspace_init(&g_kernel_vm);
    add_kernel_range(&g_rng_k_init, init_addr, &_init_end, "init");
    add_kernel_range(&g_rng_k_text, &_text_addr, &_text_end, "text");
    add_kernel_range(&g_rng_k_rodata, &_rodata_addr, rodata_end, "rodata");
    add_kernel_range(&g_rng_k_data, &_data_addr, data_end, "data");

    // 计算 per cpu 占据的大小，相邻 per cpu 之间留有 guard page
    // per cpu 应该按照 L1 一组缓存的大小对齐，这样各 CPU 的缓存使用情况一致
    // TODO 可以适当增加 smp_skip，支持 smp-alloc
    size_t pcpu_size = (size_t)(&_pcpu_bss_end - &_pcpu_addr);
    pcpu_size +=   PAGE_SIZE - 1;
    pcpu_size &= ~(PAGE_SIZE - 1);
    size_t pcpu_skip = pcpu_size + PAGE_SIZE + l1_size - 1;
    pcpu_skip &= ~(l1_size - 1);

    // 复制每个 per cpu 的内容，记录段偏移量
    size_t copy_size = (size_t)(&_pcpu_data_end - &_pcpu_addr);
    size_t zero_size = pcpu_size - copy_size;
    uint8_t *addr = (uint8_t *)(g_rng_k_data.va + g_rng_k_data.size + PAGE_SIZE);
    for (int i = 0; i < g_loapic_count; ++i) {
        memcpy(addr, &_pcpu_addr, copy_size); // 前一段需要拷贝
        memset(addr + copy_size, 0, zero_size); // 后一段只需清零
        add_kernel_range(&g_rng_k_pcpu[i], addr, addr + pcpu_size, "pcpu");
        g_pcpu_offsets[i] = (size_t)(addr - &_pcpu_addr);
        addr += pcpu_skip;
    }

    // 获取 kernel image 起止页边界（从 init 到最后一个 pcpu）
    vmrange_t *first_rng = containerof(g_kernel_vm.dl_head.next, vmrange_t, dl);
    vmrange_t *last_rng = containerof(g_kernel_vm.dl_head.prev, vmrange_t, dl);
    ASSERT(first_rng != last_rng);
    size_t kernel_end_pa = last_rng->va + last_rng->size - KERNEL_TEXT_BASE;
    size_t kernel_pfn_start = (first_rng->va - KERNEL_TEXT_BASE) >> PAGE_SHIFT;
    size_t kernel_pfn_end = (kernel_end_pa + PAGE_SIZE - 1) >> PAGE_SHIFT;
    ASSERT((KERNEL_LOAD_ADDR >> PAGE_SHIFT) == kernel_pfn_start);

    // 遍历物理内存范围，跳过 1M 之下与内核占用的部分
    for (int i = 0; i < g_range_count; ++i) {
        size_t start = (g_ranges[i].start + PAGE_SIZE - 1) >> PAGE_SHIFT;
        size_t end = g_ranges[i].end >> PAGE_SHIFT;

        if (start < 0x100) {
            start = 0x100;
        }
        if (start >= end) {
            continue;
        }

        if ((start <= kernel_pfn_start) && (kernel_pfn_end <= end)) {
            add_free_pages(start, kernel_pfn_start - start);
            add_free_pages(kernel_pfn_end, end - kernel_pfn_end);
        } else {
            add_free_pages(start, end - start);
        }
    }

    // 遍历每个 kernel range，如果之间存在 gap，则回收中间的物理页
    size_t prev_end = first_rng->va + first_rng->size;
    for (dlnode_t *i = first_rng->dl.next; &g_kernel_vm.dl_head != i; i = i->next) {
        vmrange_t *rng = containerof(i, vmrange_t, dl);
        if (prev_end + PAGE_SIZE > rng->va) {
            prev_end = rng->va + rng->size;
            continue;
        }

        pfn_t gap_size = (pfn_t)((rng->va - prev_end) >> PAGE_SHIFT);
        pfn_t gap_start = (pfn_t)((prev_end - KERNEL_TEXT_BASE) >> PAGE_SHIFT);
        add_free_pages(gap_start, gap_size);
        prev_end = rng->va + rng->size;
    }

    // 创建内核页表
    g_kernel_map = mmu_create();

    // 映射所有物理页面，至少 4G
    // TODO 将 ram 和 mmio 范围分开映射
    size_t dmap_size = npages;
    if (dmap_size < (1U << (32 - PAGE_SHIFT))) {
        dmap_size = (1U << (32 - PAGE_SHIFT));
    }
    mmu_map(g_kernel_map, DIRECT_MAP_BASE, 0, dmap_size, PAGE_WRITE);

    // 映射刚才注册的 kernel range
    map_kernel_range(&g_rng_k_init, PAGE_WRITE|PAGE_EXEC);
    map_kernel_range(&g_rng_k_text, PAGE_EXEC|PAGE_GLOBAL);
    map_kernel_range(&g_rng_k_rodata, PAGE_GLOBAL);
    map_kernel_range(&g_rng_k_data, PAGE_WRITE|PAGE_GLOBAL);
    for (int i = 0; i < g_loapic_count; ++i) {
        map_kernel_range(&g_rng_k_pcpu[i], PAGE_WRITE|PAGE_GLOBAL);
    }
}


// 回收 init section
// 移除 vmrange，清除页表项，回收物理页
void mem_reclaim_init() {
    vmspace_remove_range(&g_kernel_vm, &g_rng_k_init);

    size_t addr = g_rng_k_init.va;
    size_t npages = g_rng_k_init.size >> PAGE_SHIFT;
    add_free_pages(addr >> PAGE_SHIFT, npages);
    mmu_unmap(g_kernel_map, addr, npages, FLUSH_TLB);
}


//------------------------------------------------------------------------------
// 分页错误处理逻辑
//------------------------------------------------------------------------------

void handle_pagefault(int_context_t *ctx) {
    uint64_t va = read_cr2();
    dbg_print("virtual address %lx\n", va);

    if (0 == (1 & ctx->err)) {
        dbg_print("target address not exist\n");
    }
    if (2 & ctx->err) {
        dbg_print("target cannot be written\n");
    } else {
        dbg_print("target cannot be read\n");
    }

    void *frames[32];
    frames[0] = (void *)ctx->rip;
    int depth = unwind_from(&frames[1], 31, ctx->rbp);
    dbg_show_trace(frames, depth);

    // 检查所在的 range
    vmrange_t *rng = vmspace_locate_range(&g_kernel_vm, va);
    if (NULL != rng) {
        dbg_print("within vmrange %s\n", rng->desc);
    }

    // 现阶段，页错误无法恢复
    // 尚不支持 demand-paging
    cpu_halt();
}
