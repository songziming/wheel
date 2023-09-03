#ifndef MEM_PAGE_H
#define MEM_PAGE_H

#include <base_def.h>


typedef struct page {
    pfn_t prev;
    pfn_t next;
} page_t;

INIT_TEXT void pages_init(size_t start, size_t end);

#endif // MEM_PAGE_H
