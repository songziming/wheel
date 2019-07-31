#include <wheel.h>

#define NO_OBJ ((u16) -1)

//------------------------------------------------------------------------------
// slab level object alloc and free

static pfn_t slab_alloc(int blk_order, int obj_size) {
    pfn_t slab = page_block_alloc_or_fail(ZONE_NORMAL|ZONE_DMA, blk_order);
    u8  * addr = (u8 *) phys_to_virt((usize) slab << PAGE_SHIFT);

    if (NO_PAGE == slab) {
        return NO_PAGE;
    }

    int page_count = 1 << blk_order;
    for (int i = 0; i < page_count; ++i) {
        page_array[slab+i].type  = PT_POOL;
        page_array[slab+i].block = 0;
        page_array[slab+i].order = blk_order;
    }
    page_array[slab].type    = PT_POOL;
    page_array[slab].block   = 1;
    page_array[slab].order   = blk_order;
    page_array[slab].inuse   = 0;
    page_array[slab].objects = 0;

    int slab_size = PAGE_SIZE << blk_order;
    int obj_count = slab_size / obj_size;
    for (int i = 0; i < obj_count; ++i) {
        * (u16 *) (addr + i * obj_size) = (i + 1) * obj_size;
    }
    * (u16 *) (addr + (obj_count - 1) * obj_size) = NO_OBJ;

    return slab;
}

static void * slab_obj_alloc(pfn_t slab) {
    assert(page_array[slab].type    == PT_POOL);
    assert(page_array[slab].block   == 1);
    assert(page_array[slab].objects != NO_OBJ);

    u8 * addr = (u8 *) phys_to_virt((usize) slab << PAGE_SHIFT);
    u16  head = page_array[slab].objects;
    u16  next = * (u16 *) (addr + head);

    page_array[slab].objects = next;
    page_array[slab].inuse  += 1;

    return (void *) (addr + head);
}

static void slab_obj_free(pfn_t slab, void * obj) {
    assert(page_array[slab].type  == PT_POOL);
    assert(page_array[slab].block == 1);
    assert(page_array[slab].inuse != 0);

    usize base = (usize) phys_to_virt((usize) slab << PAGE_SHIFT);
    u16   head = page_array[slab].objects;
    * (u16 *) obj = head;

    page_array[slab].objects = (usize) obj - base;
    page_array[slab].inuse  -= 1;
}

//------------------------------------------------------------------------------
// create and destroy pool object

void pool_init(pool_t * pool, usize obj_size) {
    obj_size = ROUND_UP(obj_size, sizeof(usize));

    // find the smallest block order that holds at least 8 objects
    int blk_order = 0;
    for (; blk_order < ORDER_COUNT; ++blk_order) {
        if ((8 * obj_size) <= (PAGE_SIZE << blk_order)) {
            break;
        }
    }

    if (blk_order == ORDER_COUNT) {
        panic("pool obj_size too large!\n");
    }

    // initialize member variables
    pool->spin      = SPIN_INIT;
    pool->obj_size  = obj_size;
    pool->blk_order = blk_order;
    pool->full      = PGLIST_INIT;
    pool->partial   = PGLIST_INIT;
    pool->empty     = PGLIST_INIT;
}

void pool_destroy(pool_t * pool) {
    raw_spin_take(&pool->spin);
    pglist_free_all(&pool->full);
    pglist_free_all(&pool->partial);
    pglist_free_all(&pool->empty);
}

void pool_shrink(pool_t * pool) {
    raw_spin_take(&pool->spin);
    pglist_free_all(&pool->full);
    raw_spin_give(&pool->spin);
}

//------------------------------------------------------------------------------
// pool level object alloc and free

void * pool_alloc(pool_t * pool) {
    raw_spin_take(&pool->spin);

    if (pglist_is_empty(&pool->partial)) {
        if (pglist_is_empty(&pool->full)) {
            // create a new slab, and push to the partial tail
            pfn_t slab = slab_alloc(pool->blk_order, pool->obj_size);
            if (NO_PAGE == slab) {
                return NULL;
            }
            pglist_push_tail(&pool->partial, slab);
        } else {
            // pop a slab out from full list, and push to the partial tail
            pfn_t slab = pglist_pop_head(&pool->full);
            pglist_push_tail(&pool->partial, slab);
        }
    }

    // get the tail of partial list, but do not remove it from the list
    pfn_t  slab = pool->partial.tail;
    void * obj  = slab_obj_alloc(slab);

    // check if this slab become empty
    if (NO_OBJ == page_array[slab].objects) {
        pglist_remove(&pool->partial, slab);
        pglist_push_head(&pool->empty, slab);
    }

    raw_spin_give(&pool->spin);
    return obj;
}

void pool_free(pool_t * pool, void * obj) {
    raw_spin_take(&pool->spin);

    pfn_t slab = parent_block((pfn_t) (virt_to_phys(obj) >> PAGE_SHIFT));
    u16   head = page_array[slab].objects;
    slab_obj_free(slab, obj);

    // if empty slab becomes partial
    if (NO_OBJ == head) {
        pglist_remove(&pool->empty, slab);
        pglist_push_tail(&pool->partial, slab);
        raw_spin_give(&pool->spin);
        return;
    }

    // if partial slab becomes full
    if (0 == page_array[slab].inuse) {
        pglist_remove(&pool->partial, slab);
        pglist_push_head(&pool->full, slab);
        raw_spin_give(&pool->spin);
        return;
    }

    // slab remains partial, move this slab forward
    pfn_t prev = page_array[slab].prev;
    while ((NO_PAGE != prev) && (page_array[slab].inuse < page_array[prev].inuse)) {
        prev = page_array[prev].prev;
    }

    // if no need to move
    if (prev == page_array[slab].prev) {
        raw_spin_give(&pool->spin);
        return;
    }

    // first take `slab` out from the list
    pglist_remove(&pool->partial, slab);

    // then insert `slab` after `prev`
    if (NO_PAGE == prev) {
        pglist_push_head(&pool->partial, slab);
    } else {
        pfn_t next = page_array[prev].next;
        page_array[slab].prev = prev;
        page_array[slab].next = next;
        page_array[prev].next = slab;
        if (NO_PAGE != next) {
            page_array[next].prev = slab;
        } else {
            pool->partial.tail = slab;
        }
    }

    raw_spin_give(&pool->spin);
}
