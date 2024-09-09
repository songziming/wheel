#ifndef PAGE_H
#define PAGE_H

#include <common.h>

// 物理页类型
typedef enum page_type {
    PT_INVALID  = 0,    // 不存在或不可用
    PT_FREE,            // 未分配的可用内存
    PT_KERNEL,          // 被内核代码数据占用
    PT_PGTBL,           // 页表结构使用
    PT_KERNEL_STACK,    // 任务的内核栈
    PT_CACHE,           // 块设备缓存
} page_type_t;

INIT_TEXT void page_init(size_t end);
INIT_TEXT void page_set_type(pfn_t start, pfn_t end, uint8_t type);

#endif // PAGE_H
