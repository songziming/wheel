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
static INIT_BSS vmrange_t g_range_init;
static CONST    vmrange_t g_range_text;
static CONST    vmrange_t g_range_rodata;   // 结束位置由 g_ro_buff 决定
static CONST    vmrange_t g_range_data;     // 结束位置由 g_rw_buff 决定

static shell_cmd_t g_cmd_vm;



// 将一段内存标记为内核占用，记录在地址空间里，也记录在物理内存管理器中
// 地址必须按页对齐，因为涉及到物理页管理
static INIT_TEXT void add_kernel_range(vmspace_t *space, vmrange_t *rng,
        void *addr, void *end, const char *desc) {
    ASSERT(NULL != space);
    ASSERT(NULL != rng);

    vmspace_insert(space, rng, (size_t)addr, (size_t)end, desc);

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


static void map_kernel_range(size_t table, const vmrange_t *rng, mmu_attr_t attrs) {
    ASSERT(INVALID_ADDR != table);
    ASSERT(NULL != rng);

    size_t from = rng->addr & ~(PAGE_SIZE - 1);
    size_t to = (rng->end + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    mmu_map(table, from, to, from - KERNEL_TEXT_ADDR, attrs);
}


static int show_vm(int argc, char *argv[]) {
    (void)argc;
    (void)argv;
    vmspace_show(get_kernel_vmspace());
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
    pcpu_prepare();         // 准备 PCPU 相关数据结构
    early_alloc_disable();  // 禁用临时内存分配

    // 记录内核的地址空间布局，也是后面建立页表的依据
    vmspace_t *space = get_kernel_vmspace();
    char *kernel_addr = (char *)KERNEL_TEXT_ADDR + KERNEL_LOAD_ADDR;
    add_kernel_range(space, &g_range_init, kernel_addr, &_init_end, "init");
    add_kernel_range(space, &g_range_text, &_text_addr, &_text_end, "text");
    add_kernel_range(space, &g_range_rodata, &_rodata_addr, early_alloc_ro(0), "rodata");
    add_kernel_range(space, &g_range_data, &_data_addr, early_alloc_rw(0), "data");

    // 为 PCPU 划分空间，并将信息记录在 vmspace 中
    // PCPU 结束位置也是内核静态 sections 结束位置
    size_t kend_va = (size_t)early_alloc_rw(0);
    size_t kend_pa = pcpu_allocate(kend_va, space) - KERNEL_TEXT_ADDR;

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

    // 相邻的 range 之间还有空隙，虚拟地址保留作为 guard page，但是物理页可以使用
    for (dlnode_t *i = space->head.next->next; i != &space->head; i = i->next) {
        vmrange_t *prev = containerof(i->prev, vmrange_t, dl);
        vmrange_t *curr = containerof(i, vmrange_t, dl);
        add_kernel_gap(prev, curr);
    }

    // 注册 vmspace 命令
    g_cmd_vm.name = "vm";
    g_cmd_vm.func = show_vm;
    shell_add_cmd(&g_cmd_vm);
}


// 创建内核页表
INIT_TEXT void kernel_proc_init() {
    ASSERT(NULL != g_pmmap);
    ASSERT(g_pmmap_len > 0);

    // 创建页表
    uint64_t table = get_kernel_pgtable();
    vmspace_t *space = get_kernel_vmspace();

    // 映射内核代码数据段
    map_kernel_range(table, &g_range_init, MMU_WRITE|MMU_EXEC);
    map_kernel_range(table, &g_range_text, MMU_EXEC);
    map_kernel_range(table, &g_range_rodata, MMU_NONE);
    map_kernel_range(table, &g_range_data, MMU_WRITE);

    // 遍历剩下的 pcpu 和异常栈
    ASSERT(dl_contains(&space->head, &g_range_data.dl));
    for (dlnode_t *i = g_range_data.dl.next; i != &space->head; i = i->next) {
        map_kernel_range(table, containerof(i, vmrange_t, dl), MMU_WRITE);
    }

    // 将所有物理内存（至少 4GB，包含 MMIO 部分）映射到内核地址空间
    // TODO 遍历所有物理内存范围 pmmap，逐个映射
    uint64_t maplen = 1UL << 32; // g_pmmap[g_pmmap_len - 1].end;
    mmu_map(table, DIRECT_MAP_ADDR, DIRECT_MAP_ADDR + maplen, 0, MMU_WRITE);
}


// 回收 init 部分的内存空间（本函数不能使用 init 函数）
// 需要在 root-task 中执行，此时已切换到任务栈，不再使用初始栈
void reclaim_init() {
    // 注意 page_add 也是初始代码段函数，但此时可以调用
    // TODO 我们已经把 init 标记为了 PT_KERNEL，重新执行 page_add 会出错
    //      应该使用 page_range_free
    size_t from = g_range_init.addr & ~(PAGE_SIZE - 1);
    size_t to = (g_range_init.end + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    page_add(from - KERNEL_TEXT_ADDR, to - KERNEL_TEXT_ADDR, PT_KERNEL);

    // 移除 init 部分的映射
    // 需要首先读出 init 变量，避免再次访问
    size_t init_start = g_range_init.addr;
    size_t init_end = g_range_init.end;
    vmspace_remove(get_kernel_vmspace(), &g_range_init);
    mmu_unmap(get_kernel_pgtable(), init_start, init_end);
}
