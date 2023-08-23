// 物理内存范围管理、地址空间管理


#include <arch_mem.h>
#include <arch_api.h>
#include <debug.h>
#include <libk_string.h>

#include <early_buff.h>


//------------------------------------------------------------------------------
// 启动阶段的内存分配
//------------------------------------------------------------------------------

static SECTION(".rotail") uint8_t g_const_area[EARLY_CONST_BUFF_SIZE];
static SECTION(".tail") uint8_t g_alloc_area[EARLY_ALLOC_BUFF_SIZE];
static INIT_DATA early_buff_t g_early_const_buff;
static INIT_DATA early_buff_t g_early_alloc_buff;

INIT_TEXT void early_alloc_init() {
    early_buff_init(&g_early_const_buff, g_const_area, g_const_area + EARLY_CONST_BUFF_SIZE);
    early_buff_init(&g_early_alloc_buff, g_alloc_area, g_alloc_area + EARLY_ALLOC_BUFF_SIZE);
}

INIT_TEXT void *early_alloc(size_t size) {
    void *p = early_buff_alloc(&g_early_alloc_buff, size);
    if (NULL == p) {
        dbg_print("early alloc buffer overflow!\n");
        cpu_halt();
    }
    return p;
}

INIT_TEXT void *early_const_alloc(size_t size) {
    void *p = early_buff_alloc(&g_early_const_buff, size);
    if (NULL == p) {
        dbg_print("early const buffer overflow!\n");
        p = early_buff_alloc(&g_early_alloc_buff, size);
    }
    if (NULL == p) {
        dbg_print("cannot alloc for const data!\n");
        cpu_halt();
    }
    return p;
}

// 解锁长度限制，可以超过配置的 BUFF_SIZE，只要物理内存足够就能一直分配
INIT_TEXT void kernel_end_unlock() {
    size_t alloc_pa = (size_t)g_alloc_area - KERNEL_TEXT_BASE;
    size_t alloc_end = ram_range_get_limit(alloc_pa) + KERNEL_TEXT_BASE;
    early_buff_set_end(&g_early_alloc_buff, (void *)alloc_end);

    size_t const_pa = (size_t)g_const_area - KERNEL_TEXT_BASE;
    size_t const_end = ram_range_get_limit(const_pa) + KERNEL_TEXT_BASE;
    early_buff_set_end(&g_early_const_buff, (void *)const_end);
}

// 禁止使用 early_alloc 分配内存，已分配的部分固定下来
INIT_TEXT void early_alloc_disable() {
    early_buff_set_end(&g_early_const_buff, early_buff_get_cur(&g_early_const_buff));
    early_buff_set_end(&g_early_alloc_buff, early_buff_get_cur(&g_early_alloc_buff));
}

//------------------------------------------------------------------------------
// 物理内存布局管理
//------------------------------------------------------------------------------

static CONST int g_ram_range_num = 0;
static CONST size_t *g_ram_ranges = NULL; // 两个元素一组，左闭右开

INIT_TEXT void ram_range_reserve(int num) {
    ASSERT(0 == g_ram_range_num);
    ASSERT(NULL == g_ram_ranges);

    if (num <= 0) {
        return;
    }

    g_ram_range_num = num;
    g_ram_ranges = early_const_alloc(num * 2 * sizeof(size_t));
    kmemset(g_ram_ranges, 0, num * 2 * sizeof(size_t));
}

// 添加一段物理内存
INIT_TEXT void ram_range_set(int idx, size_t addr, size_t len) {
    ASSERT(NULL != g_ram_ranges);
    ASSERT(idx >= 0);
    ASSERT(idx < g_ram_range_num);

    g_ram_ranges[idx * 2 + 0] = addr;
    g_ram_ranges[idx * 2 + 1] = addr + len;
}

// 返回 addr 之后能连续访问到的最高地址
INIT_TEXT size_t ram_range_get_limit(size_t addr) {
    ASSERT(NULL != g_ram_ranges);

    for (int i = 0; i < g_ram_range_num; ++i) {
        size_t start = g_ram_ranges[i*2 + 0];
        size_t end = g_ram_ranges[i*2 + 1];
        if ((start <= addr) && (addr < end)) {
            return end;
        }
    }

    dbg_print("address %lx not in valid ram range\n", addr);
    return 0;
}
