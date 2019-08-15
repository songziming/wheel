#include <wheel.h>

page_t * page_array;
usize    page_count;

typedef struct zone {
    spin_t   spin;
    usize    page_count;
    pglist_t list[ORDER_COUNT]; // block list of each order
} zone_t;

static zone_t zone_dma;
static zone_t zone_normal;

//------------------------------------------------------------------------------
// page list operations

int pglist_is_empty(pglist_t * list) {
    if ((NO_PAGE == list->head) && (NO_PAGE == list->tail)) {
        return YES;
    } else {
        return NO;
    }
}

void pglist_push_head(pglist_t * list, pfn_t page) {
    page_array[page].prev = NO_PAGE;
    page_array[page].next = list->head;
    if (NO_PAGE != list->head) {
        page_array[list->head].prev = page;
    } else {
        list->tail = page;
    }
    list->head = page;
}

void pglist_push_tail(pglist_t * list, pfn_t page) {
    page_array[page].prev = list->tail;
    page_array[page].next = NO_PAGE;
    if (NO_PAGE != list->tail) {
        page_array[list->tail].next = page;
    } else {
        list->head = page;
    }
    list->tail = page;
}

pfn_t pglist_pop_head(pglist_t * list) {
    pfn_t head = list->head;
    if (NO_PAGE != head) {
        list->head = page_array[head].next;
    }
    if (NO_PAGE == list->head) {
        list->tail = NO_PAGE;
    }
    return head;
}

pfn_t pglist_pop_tail(pglist_t * list) {
    pfn_t tail = list->tail;
    if (NO_PAGE != tail) {
        list->tail = page_array[tail].prev;
    }
    if (NO_PAGE == list->tail) {
        list->head = NO_PAGE;
    }
    return tail;
}

void pglist_remove(pglist_t * list, pfn_t page) {
    pfn_t prev = page_array[page].prev;
    pfn_t next = page_array[page].next;
    if (NO_PAGE != prev) {
        page_array[prev].next = next;
    } else {
        assert(list->head == page);
        list->head = next;
    }
    if (NO_PAGE != next) {
        page_array[next].prev = prev;
    } else {
        assert(list->tail == page);
        list->tail = prev;
    }
}

void pglist_free_all(pglist_t * list) {
    while (NO_PAGE != list->head) {
        assert(1 == page_array[list->head].block);
        pfn_t next = page_array[list->head].next;
        // page_block_free(list->head, page_array[list->head].order);
        list->head = next;
    }
    list->tail = NO_PAGE;
}

//------------------------------------------------------------------------------
// page frame allocator - private

static pfn_t zone_block_alloc(zone_t * zone, int order) {
    for (int o = order; o < ORDER_COUNT; ++o) {
        if (pglist_is_empty(&zone->list[o])) {
            continue;
        }

        // found an order with free block, remove its first element
        pfn_t blk = pglist_pop_head(&zone->list[o]);

        // split the block, and return the second half back
        // return second half, so base address remain unchanged
        for (; o > order; --o) {
            usize size = 1UL << (o - 1);
            pfn_t bud  = blk ^ size;
            page_array[bud].block = 1;
            page_array[bud].order = o - 1;

            // return buddy block to the list
            pglist_push_head(&zone->list[o-1], bud);
        }

        // no need to fill page_array here, let caller do that
        return blk;
    }

    // no block is big enough
    return NO_PAGE;
}

static void zone_block_free(zone_t * zone, pfn_t blk, u32 order) {
    // mark this block as free
    usize size = 1U << order;
    for (pfn_t i = 0; i < size; ++i) {
        page_array[blk + i].type  = PT_FREE;
        page_array[blk + i].block = 0;
    }

    // merging into bigger block
    for (; order < ORDER_COUNT - 1; ++order) {
        size = 1U << order;
        pfn_t bud = blk ^ size;
        if ((PT_FREE != page_array[bud].type)  ||
            (1       != page_array[bud].block) ||
            (order   != page_array[bud].order)) {
            // cannot merge with buddy
            break;
        }

        // remove buddy from block list
        pglist_remove(&zone->list[order], bud);

        // merge with its buddy
        page_array[bud].block = 0;
        blk &= bud;
    }

    size = 1U << order;
    zone->page_count += size;
    page_array[blk].block = 1;
    page_array[blk].order = order;

    // put this block into the block list
    pglist_push_head(&zone->list[order], blk);
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wtype-limits"
static inline zone_t * zone_for(usize start, usize end) {
    if ((DMA_START <= start) && (end <= DMA_END)) {
        return &zone_dma;
    }
    if ((NORMAL_START <= start) && (end <= NORMAL_END)) {
        return &zone_normal;
    }
    return NULL;
}
#pragma GCC diagnostic pop

//------------------------------------------------------------------------------
// page frame allocator - public

// allocate page block of size 2^order
// lock zone and interrupt, so ISR could alloc pages
pfn_t page_block_alloc(u32 zones, int order) {
    if ((order < 0) || (order >= ORDER_COUNT)) {
        return NO_PAGE; // invalid parameter
    }

    if (zones & ZONE_NORMAL) {
        u32 key = irq_spin_take(&zone_normal.spin);
        pfn_t blk = zone_block_alloc(&zone_normal, order);
        irq_spin_give(&zone_normal.spin, key);
        assert((blk & ((1U << order) - 1)) == 0);
        if (NO_PAGE != blk) {
            return blk;
        }
    }

    if (zones & ZONE_DMA) {
        u32 key = irq_spin_take(&zone_dma.spin);
        pfn_t blk = zone_block_alloc(&zone_dma, order);
        irq_spin_give(&zone_dma.spin, key);
        assert((blk & ((1U << order) - 1)) == 0);
        if (NO_PAGE != blk) {
            return blk;
        }
    }

    // no block is large enough
    return NO_PAGE;
}

pfn_t page_block_alloc_or_fail(u32 zones, int order) {
    pfn_t blk = page_block_alloc(zones, order);

    if (NO_PAGE == blk) {
        panic("out of memory when allocating 2^%d pages from %s %s\n",
              order,
              (zones & ZONE_DMA)    ? "DMA"    : "",
              (zones & ZONE_NORMAL) ? "NORMAL" : "");
    }

    return blk;
}

void page_block_free(pfn_t blk, int order) {
    usize size = 1UL << order;
    zone_t * zone = zone_for((usize)  blk         << PAGE_SHIFT,
                             (usize) (blk + size) << PAGE_SHIFT);
    assert(order >= 0);
    assert(order < ORDER_COUNT);
    assert(0 == (blk & (size - 1)));
    assert(NULL != zone);

    raw_spin_take(&zone->spin);
    zone_block_free(zone, blk, order);
    raw_spin_give(&zone->spin);
}

pfn_t parent_block(pfn_t page) {
    while ((page != 0) && (page_array[page].block == 0)) {
        page &= page - 1;
    }
    return page;
}

usize free_page_count(u32 zones) {
    usize count = 0;
    if (zones & ZONE_DMA) {
        count += zone_dma.page_count;
    }
    if (zones & ZONE_NORMAL) {
        count += zone_normal.page_count;
    }
    return count;
}

//------------------------------------------------------------------------------
// initialize page frame allocator, initially no free page

__INIT void page_lib_init() {
    zone_dma.spin          = SPIN_INIT;
    zone_normal.spin       = SPIN_INIT;
    zone_dma.page_count    = 0;
    zone_normal.page_count = 0;
    for (int i = 0; i < ORDER_COUNT; ++i) {
        zone_dma.list[i].head    = NO_PAGE;
        zone_dma.list[i].tail    = NO_PAGE;
        zone_normal.list[i].head = NO_PAGE;
        zone_normal.list[i].tail = NO_PAGE;
    }
}

// // add a range of free memory
// __INIT void page_range_free(usize start, usize end) {
//     pfn_t from = (pfn_t) (start >> PAGE_SHIFT);
//     pfn_t to   = (pfn_t) (end   >> PAGE_SHIFT);

//     while (from < to) {
//         // compute best order for `from`
//         int order = CTZ32(from);
//         if ((order >= ORDER_COUNT) || (from == 0)) {
//             order = ORDER_COUNT - 1;
//         }
//         while ((from + (1UL << order)) > to) {
//             --order;
//         }

//         // return this block
//         page_block_free(from, order);
//         from += (1UL << order);
//     }
// }

static __INIT void dump_layout(zone_t * zone) {
    for (u32 o = 0; o < ORDER_COUNT; ++o) {
        pfn_t blk = zone->list[o].head;
        if (NO_PAGE == blk) {
            continue;
        }
        dbg_print("-- order %02u:", o);
        while (NO_PAGE != blk) {
            dbg_print(" 0x%x", blk);
            blk = page_array[blk].next;
        }
        dbg_print(".\n");
    }
}

__INIT void page_info_show(u32 zones) {
    if (zones & ZONE_DMA) {
        dbg_print("== zone dma:\n");
        dump_layout(&zone_dma);
    }
    if (zones & ZONE_NORMAL) {
        dbg_print("== zone normal:\n");
        dump_layout(&zone_normal);
    }
}
