#ifndef MEM_PAGE_H
#define MEM_PAGE_H

#include <base.h>
#include <arch.h>

//------------------------------------------------------------------------------
// page descriptor

// one for each physical page frame
typedef struct page {
    pfn_t prev;
    pfn_t next;
    u32   type  : 4;
    u32   order : 4;            // only valid when block=1
    u32   block : 1;            // is it the first page in block
    union {
        struct {                // pool
            u16 objects;        // first free object
            u16 inuse;          // number of allocated objects
        };
    };
} page_t;

// page types
#define PT_INVALID      0       // memory hole or mapped device
#define PT_FREE         1       // not allocated
#define PT_CACHED       2       // percpu cache
#define PT_KERNEL       3       // generic kernel usage
#define PT_POOL         4       // memory pool
#define PT_KSTACK       5       // task's kernel stack page
#define PT_PIPE         6       // buffer space of pipe
#define PT_FIFOBUF      7       // FIFO buffer

// block order
#define ORDER_COUNT     16

// global page array
extern page_t * page_array;
extern usize    page_count;

//------------------------------------------------------------------------------
// page list

// container of several pages, used to keep track of memory usage
typedef struct pglist {
    pfn_t head;
    pfn_t tail;
} pglist_t;

// page list initializer
#define PGLIST_INIT     ((pglist_t) { NO_PAGE, NO_PAGE })

// page list operations
extern int   pglist_is_empty (pglist_t * list);
extern void  pglist_push_head(pglist_t * list, pfn_t page);
extern void  pglist_push_tail(pglist_t * list, pfn_t page);
extern pfn_t pglist_pop_head (pglist_t * list);
extern pfn_t pglist_pop_tail (pglist_t * list);
extern void  pglist_remove   (pglist_t * list, pfn_t page);
extern void  pglist_free_all (pglist_t * list);

//------------------------------------------------------------------------------
// page frame allocator

// memory zone bit masks
#define ZONE_DMA        1
#define ZONE_NORMAL     2

// page frame allocator
extern pfn_t page_block_alloc        (u32 zones, int order);
extern pfn_t page_block_alloc_or_fail(u32 zones, int order);
extern void  page_block_free         (pfn_t blk, int order);
extern pfn_t parent_block            (pfn_t page);
extern usize free_page_count         (u32 zones);

// requires: nothing
extern __INIT void page_lib_init ();
extern __INIT void page_range_add(usize start, usize end);
extern __INIT void page_info_show(u32 zones);

#endif // MEM_PAGE_H
