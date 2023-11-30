#ifndef PAGE_H
#define PAGE_H

#include <def.h>


// 物理页类型
typedef enum page_type {
    PT_INVALID  = 0,    // 不存在
    PT_FREE,            // 未分配的可用内存
    PT_KERNEL,          // 被内核代码数据占用
} page_type_t;


// 每个物理页都有这个结构体，记录相关信息
typedef union page_info_ {
    struct {
        uint16_t    ent_num;    // 有效页表条目数量
    };
} page_info_t;




INIT_TEXT void page_init(size_t end);
INIT_TEXT void page_add(size_t start, size_t end, page_type_t type);



size_t page_alloc(page_type_t type);    // 申请一个物理页，返回物理地址
void page_free(size_t pa);

// 为了安全，可以将 page_info 复制一份返回，避免外部代码直接修改页描述符
page_info_t *page_info(size_t pa);

#endif // PAGE_H
