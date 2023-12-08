// 内核地址空间管理

#include <arch_mem.h>
#include <arch_cpu.h>
#include <arch_mmu.h>
#include <arch_smp.h>
#include <arch_api_p.h>

#include <wheel.h>
#include <vmspace.h>
#include <page.h>
#include <str.h>



// layout.ld
extern char _init_end;
extern char _text_addr, _text_end;
extern char _rodata_addr;
extern char _data_addr;

// 内核虚拟地址空间和页表
static vmspace_t g_kernel_vm;
static uint64_t g_kernel_cr3 = INVALID_ADDR;

// pmmap.c，物理内存布局
extern CONST int g_pmmap_len;
extern CONST pmrange_t *g_pmmap;

// 初始化代码数据，还有 PCPU 模板、实模式代码
static INIT_BSS vmrange_t  g_range_init;
static CONST    vmrange_t  g_range_text;
static CONST    vmrange_t  g_range_rodata;   // 结束位置由 g_ro_buff 决定
static CONST    vmrange_t  g_range_data;     // 结束位置由 g_rw_buff 决定


// 将一段内存标记为内核占用，记录在地址空间里，也记录在物理内存管理器中
// 地址必须按页对齐，因为涉及到物理页管理
static INIT_TEXT void add_kernel_range(vmrange_t *rng, void *addr, void *end, const char *desc) {
    rng->addr = (size_t)addr;
    rng->end  = (size_t)end;
    rng->desc = desc;
    vmspace_add(&g_kernel_vm, rng);

    size_t from = rng->addr & ~(PAGE_SIZE - 1);
    size_t to = (rng->end + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    page_add(from - KERNEL_TEXT_ADDR, to - KERNEL_TEXT_ADDR, PT_KERNEL);
}


// 将两段内核范围之间的页面回收，这部分虚拟地址作为 guard page，但物理页还可以用
static INIT_TEXT void add_kernel_gap(vmrange_t *prev, vmrange_t *curr) {
    ASSERT(NULL != prev);
    ASSERT(NULL != curr);
    ASSERT(prev != curr);
    ASSERT(prev->dl.next == &curr->dl);
    ASSERT(curr->dl.prev == &prev->dl);

    size_t from = (prev->end + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    size_t to = curr->addr & ~(PAGE_SIZE - 1);
    page_add(from - KERNEL_TEXT_ADDR, to - KERNEL_TEXT_ADDR, PT_KERNEL);
}


static void map_kernel_range(const vmrange_t *rng, mmu_attr_t attrs) {
    ASSERT(INVALID_ADDR != g_kernel_cr3);

    size_t from = rng->addr & ~(PAGE_SIZE - 1);
    size_t to = (rng->end + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    mmu_map(g_kernel_cr3, from, to, from - KERNEL_TEXT_ADDR, attrs);
}


// 划分内存，停用 early-alloc，启用物理页分配
INIT_TEXT void mem_init() {
    ASSERT(NULL != g_pmmap);
    ASSERT(g_pmmap_len > 0);

    // 获取内存上限
    size_t ramtop = 0;
    for (int i = g_pmmap_len - 1; i >= 0; --i) {
        pmtype_t type = g_pmmap[i].type;
        if ((PM_AVAILABLE != type) && (PM_RECLAIMABLE != type)) {
            continue;
        }
        ramtop = g_pmmap[i].end & ~(PAGE_SIZE - 1);
        break;
    }

    page_init(ramtop); // 分配页描述符数组
    pcpu_prepare(); // 准备 PCPU 相关数据结构

    // 禁用临时内存分配
    early_alloc_disable();

    // 记录内核的地址空间布局，也是后面建立页表的依据
    vmspace_init(&g_kernel_vm);
    char *kernel_addr = (char *)KERNEL_TEXT_ADDR + KERNEL_LOAD_ADDR;
    add_kernel_range(&g_range_init, kernel_addr, &_init_end, "init");
    add_kernel_range(&g_range_text, &_text_addr, &_text_end, "text");
    add_kernel_range(&g_range_rodata, &_rodata_addr, early_alloc_ro(0), "rodata");
    add_kernel_range(&g_range_data, &_data_addr, early_alloc_rw(0), "data");

    // 为 PCPU 划分空间，并将信息记录在 vmspace 中
    // PCPU 结束位置也是内核静态 sections 结束位置
    size_t kend_va = (size_t)early_alloc_rw(0);
    size_t kend_pa = pcpu_allocate(kend_va, &g_kernel_vm) - KERNEL_TEXT_ADDR;

    // 显示内核地址空间布局
    vmspace_show(&g_kernel_vm);

    // 遍历物理内存范围，将可用内存添加给页分配器
    // RECLAIMABLE 也属于可用范围，跳过 1M 以下的 lowmem 与内核占用的部分
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
        if ((start <= KERNEL_LOAD_ADDR) && (end >= kend_pa)) {
            page_add(start, KERNEL_LOAD_ADDR, PT_FREE);
            page_add(kend_pa, end, PT_FREE);
        } else {
            page_add(start, end, PT_FREE);
        }
    }

    // 相邻的 vmrange 之间还有空隙，虚拟地址保留作为 guard page，但是物理页可以回收
    for (dlnode_t *i = g_kernel_vm.head.next->next; i != &g_kernel_vm.head; i = i->next) {
        vmrange_t *prev = containerof(i->prev, vmrange_t, dl);
        vmrange_t *curr = containerof(i, vmrange_t, dl);
        add_kernel_gap(prev, curr);
    }
}


// 创建内核页表
INIT_TEXT void ctx_init() {
    ASSERT(INVALID_ADDR == g_kernel_cr3);
    ASSERT(NULL != g_pmmap);
    ASSERT(g_pmmap_len > 0);

    // 创建页表
    g_kernel_cr3 = mmu_table_create();

    // 映射内核代码数据段
    map_kernel_range(&g_range_init, MMU_WRITE|MMU_EXEC);
    map_kernel_range(&g_range_text, MMU_EXEC);
    map_kernel_range(&g_range_rodata, 0);
    map_kernel_range(&g_range_data, MMU_WRITE);

    // 遍历剩下的 pcpu 和异常栈
    for (dlnode_t *i = g_range_data.dl.next; i != &g_kernel_vm.head; i = i->next) {
        map_kernel_range(containerof(i, vmrange_t, dl), MMU_WRITE);
    }

    // 将所有物理内存映射到内核地址空间
    // 至少映射 4GB，因为涉及到 APIC、framebuf 等硬件设备
    // 物理内存布局中，不仅会列出 ram，还包硬件映射
    uint64_t maplen = g_pmmap[g_pmmap_len - 1].end;
    mmu_map(g_kernel_cr3, DIRECT_MAP_ADDR, DIRECT_MAP_ADDR + maplen, 0, MMU_WRITE);

    // 切换到新页表
    write_cr3(g_kernel_cr3);
}


// 回收 init 部分的内存空间（本函数不能使用 init 函数）
void reclaim_init() {
    ASSERT(INVALID_ADDR != g_kernel_cr3);

    // 注意 page_add 也是初始代码段函数，但此时可以调用
    // TODO 我们已经把 init 标记为了 PT_KERNEL，重新执行 page_add 会出错
    //      应该使用 page_range_free
    size_t from = g_range_init.addr & ~(PAGE_SIZE - 1);
    size_t to = (g_range_init.end + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    page_add(from - KERNEL_TEXT_ADDR, to - KERNEL_TEXT_ADDR, PT_KERNEL);

    // TODO 移除 init 部分的映射
    // mmu_unmap
}
