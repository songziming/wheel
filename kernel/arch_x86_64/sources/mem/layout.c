// 内核地址空间管理

#include <arch_mem.h>
#include <arch_smp.h>
#include <cpu/rw.h>

#include <wheel.h>
#include <shell.h>



// layout.ld
extern char _init_end;
extern char _text_addr, _text_end;
extern char _rodata_addr;
extern char _data_addr;

// pmmap.c，物理内存布局
extern CONST int g_pmmap_len;
extern CONST pmrange_t *g_pmmap;

// 初始化代码数据，还有 PCPU 模板、实模式代码
// 这些 range 不能是 const，因为处于链表中，需要动态增删
static          vmrange_t g_range_idmap;
static INIT_BSS vmrange_t g_range_init;
static          vmrange_t g_range_text;
static          vmrange_t g_range_rodata;   // 结束位置由 g_ro_buff 决定
static          vmrange_t g_range_data;     // 结束位置由 g_rw_buff 决定

// 给内核堆预留的 range
static vmrange_t g_range_kheap;
// static mem_heap_t g_common_heap = { SPIN_INIT, RBTREE_INIT, NULL, NULL };
// static uint8_t g_heap_buff[KERNEL_HEAP_SIZE];

static shell_cmd_t g_cmd_vm;



// 将一段内存标记为内核占用，记录在地址空间里，也记录在物理内存管理器中
// 地址必须按页对齐，因为涉及到物理页管理
static INIT_TEXT void mark_kernel_section(vmrange_t *rng,
        void *addr, void *end, mmu_attr_t attrs, const char *desc) {
    // ASSERT(NULL != space);
    ASSERT(NULL != rng);

    size_t pa = (size_t)addr - KERNEL_TEXT_ADDR;
    // vmspace_insert(space, rng, (size_t)addr, (size_t)end, pa, attrs, desc);
    kernel_context_mark(rng, (size_t)addr, (size_t)end, pa, attrs, desc);

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
    page_add(from - KERNEL_TEXT_ADDR, to - KERNEL_TEXT_ADDR, PT_FREE);
}


// static void map_kernel_range(size_t table, const vmrange_t *rng, mmu_attr_t attrs) {
//     ASSERT(INVALID_ADDR != table);
//     ASSERT(NULL != rng);

//     size_t from = rng->addr & ~(PAGE_SIZE - 1);
//     size_t to = (rng->end + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
//     mmu_map(table, from, to, from - KERNEL_TEXT_ADDR, attrs);
// }


static int show_vm(int argc UNUSED, char *argv[] UNUSED) {
    context_t *kctx = get_kernel_context();

    klog("virtual mem space:\n");
    for (dlnode_t *i = kctx->head.next; &kctx->head != i; i = i->next) {
        vmrange_t *rng = containerof(i, vmrange_t, dl);
        klog("  - 0x%zx..0x%zx %s\n", rng->addr, rng->end, rng->desc);
    }

    return 0;
}


// 内存管理初始化，具体包括：
//  - 禁用临时内存分配 early-alloc
//  - 记录各 section 的内存布局，并为 page-array、pcpu 划分空间
//  - 将内核未占用的物理页添加到 page-alloc
INIT_TEXT void mem_init() {
    ASSERT(NULL != g_pmmap);
    ASSERT(g_pmmap_len > 0);

    // 获取可用内存上限
    size_t ramtop = 0;
    for (int i = g_pmmap_len - 1; i >= 0; --i) {
        pmtype_t type = g_pmmap[i].type;
        if ((PM_AVAILABLE != type) && (PM_RECLAIMABLE != type)) {
            continue;
        }
        ramtop = g_pmmap[i].end & ~(PAGE_SIZE - 1);
        break;
    }

    page_init(ramtop);      // 分配页描述符数组
    early_alloc_disable();  // 禁用临时内存分配

    // 获取当前内核结束位置（的虚拟地址）
    char *kend_va = early_alloc_rw(0);

    // 记录内核的地址空间布局，也是后面建立页表的依据
    char *kernel_addr = (char *)KERNEL_TEXT_ADDR + KERNEL_LOAD_ADDR;
    mark_kernel_section(&g_range_init, kernel_addr, &_init_end, MMU_WRITE|MMU_EXEC, "kernel init");
    mark_kernel_section(&g_range_text, &_text_addr, &_text_end, MMU_EXEC, "kernel text");
    mark_kernel_section(&g_range_rodata, &_rodata_addr, early_alloc_ro(0), MMU_NONE, "kernel rodata");
    mark_kernel_section(&g_range_data, &_data_addr, kend_va, MMU_WRITE, "kernel data");

    // 起始地址按页对齐，并留出一个 guard page
    kend_va += 2 * PAGE_SIZE - 1;
    kend_va = (char *)((size_t)kend_va & ~(PAGE_SIZE - 1));
    mark_kernel_section(&g_range_kheap, kend_va, kend_va + KERNEL_HEAP_SIZE, MMU_WRITE, "kernel heap");
    kernel_heap_init(kend_va, KERNEL_HEAP_SIZE);
    kend_va += KERNEL_HEAP_SIZE;

    // 为 PCPU 划分空间，并将信息记录在 vmspace 中
    // PCPU 结束位置也是内核静态 sections 结束位置
    pcpu_allocate((size_t)kend_va);

    // 最后一个 range，获取内核结束位置
    context_t *kctx = get_kernel_context();
    vmrange_t *last = containerof(kctx->head.prev, vmrange_t, dl);
    size_t kend_pa = last->end - last->addr + last->pa;

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

        // 内核占据的内存范围必然完整包含于一段 pmmap
        if ((start <= KERNEL_LOAD_ADDR) && (end >= kend_pa)) {
            page_add(start, KERNEL_LOAD_ADDR, PT_FREE);
            page_add(kend_pa, end, PT_FREE);
        } else {
            page_add(start, end, PT_FREE);
        }
    }

    // 相邻的 range 之间还有空隙，虚拟地址保留作为 guard page，但是物理页可以使用
    for (dlnode_t *i = kctx->head.next->next; i != &kctx->head; i = i->next) {
        vmrange_t *prev = containerof(i->prev, vmrange_t, dl);
        vmrange_t *curr = containerof(i, vmrange_t, dl);
        add_kernel_gap(prev, curr);
    }

    // 将所有物理内存（含 MMIO）映射到 canonical hole 之后
    // 最后再添加这个 mapping，防止 guard page 混乱
    // TODO 常规内存和 MMIO 分开映射？使用 pmmap 获取物理内存上限？
    size_t map_addr = DIRECT_MAP_ADDR;
    size_t map_end = map_addr + (1UL << 32); // g_pmmap[g_pmmap_len - 1].end;
    kernel_context_mark(&g_range_idmap, map_addr, map_end, 0, MMU_WRITE, "id-map");

    // 注册 vmspace 命令
    g_cmd_vm.name = "vm";
    g_cmd_vm.func = show_vm;
    shell_add_cmd(&g_cmd_vm);
}


// 回收 init 部分的内存空间（本函数不能使用 init 函数）
// 需要在 root-task 中执行，此时已切换到任务栈，不再使用初始栈
void reclaim_init() {
    size_t pa  = g_range_init.pa;
    size_t end = g_range_init.end - g_range_init.addr + g_range_init.pa;
    end = (end + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);

    // 本函数是 init，但只是把物理页标记回收，映射关系还在
    page_add(pa, end, PT_FREE);

    // 也可以将 vmrange 保留在地址空间里，方便 #PF handler 检查
    kernel_context_unmap(&g_range_init);
}
