#ifndef PAGE_H
#define PAGE_H

#include <base.h>

typedef struct page {
    pfn_t prev;
    pfn_t next;
} page_t;

INIT_TEXT void pages_init(size_t start, size_t end);

#endif // PAGE_H
