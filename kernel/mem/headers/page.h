#ifndef PAGE_H
#define PAGE_H

#include <base.h>


typedef enum page_type {
    PT_INVALID  = 0,    // 不存在
    PT_FREE,            // 未分配的可用内存
    PT_KERNEL,          // 被内核代码数据占用
} page_type_t;


typedef struct page {
    pfn_t prev;
    pfn_t next;

    size_t isblock : 1;    // 是不是块中第一个页
    size_t rank : 4;   // 所在块的大小，isblock==1 才有效
    size_t type : 4;   // 所在块的类型，isblock==1 才有效
    size_t virt : VIRT_ADDR_BITS - PAGE_SHIFT; // 映射到的虚拟地址，页对齐，不用保存符号扩展部分

    // 各种类型页的额外信息
    // union {
    //     //
    // } u;
} page_t;

INIT_TEXT void pages_init(size_t start, size_t end);

INIT_TEXT void pages_add(pfn_t start, pfn_t end);

#endif // PAGE_H
