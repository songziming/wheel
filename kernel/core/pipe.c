#include <wheel.h>

void pipe_init(pipe_t * pipe) {
    pipe->lock     = SPIN_INIT;
    pipe->pages    = PGLIST_INIT;
    pipe->r_offset = 0;
    pipe->w_offset = 0;

    pfn_t pn = page_block_alloc_or_fail(ZONE_NORMAL|ZONE_DMA, 0);
    page_array[pn].block = 1;
    page_array[pn].order = 0;
    page_array[pn].type  = PT_PIPE;
    pglist_push_tail(&pipe->pages, pn);
}

void pipe_destroy(pipe_t * pipe) {
    raw_spin_take(&pipe->lock);
    pglist_free_all(&pipe->pages);
}

usize pipe_read(pipe_t * pipe, u8 * buf, usize len) {
    usize backup_len = len;
    raw_spin_take(&pipe->lock);

    if ((pipe->pages.head  == pipe->pages.tail) &&
        (pipe->r_offset == pipe->w_offset)) {
        raw_spin_give(&pipe->lock);
        return 0;
    }

    while (len) {
        if (pipe->pages.head == pipe->pages.tail) {
            // same page, make sure r_offset does not exceeds w_offset
            pfn_t head = pipe->pages.head;
            u8  * addr = phys_to_virt((usize) head << PAGE_SHIFT);
            usize copy = MIN(len, (usize) pipe->w_offset - pipe->r_offset);
            memcpy(buf, addr + pipe->r_offset, copy);
            pipe->r_offset += copy;
            buf            += copy;
            len            -= copy;

            raw_spin_give(&pipe->lock);
            return backup_len - len;
        }

        // r_offset and w_offset in different pages
        pfn_t head = pipe->pages.head;
        u8  * addr = phys_to_virt((usize) head << PAGE_SHIFT);
        usize copy = MIN(len, (usize) PAGE_SIZE - pipe->r_offset);
        memcpy(buf, addr + pipe->r_offset, copy);
        pipe->r_offset += copy;
        buf            += copy;
        len            -= copy;

        // free head page if all content have been read
        if (PAGE_SIZE == pipe->r_offset) {
            head = pglist_pop_head(&pipe->pages);
            page_block_free(head, 0);
            pipe->r_offset = 0;
        }
    }

    raw_spin_give(&pipe->lock);
    return backup_len;
}

usize pipe_write(pipe_t * pipe, u8 * buf, usize len) {
    raw_spin_take(&pipe->lock);
    usize backup_len = len;

    while (len) {
        pfn_t tail = pipe->pages.tail;
        u8  * addr = phys_to_virt((usize) tail << PAGE_SHIFT);
        usize copy = MIN(len, (usize) PAGE_SIZE - pipe->w_offset);
        memcpy(addr + pipe->w_offset, buf, copy);
        pipe->w_offset += copy;
        buf            += copy;
        len            -= copy;

        if (PAGE_SIZE == pipe->w_offset) {
            tail = page_block_alloc_or_fail(ZONE_NORMAL|ZONE_DMA, 0);
            pglist_push_tail(&pipe->pages, tail);
            page_array[tail].block = 1;
            page_array[tail].order = 0;
            page_array[tail].type  = PT_PIPE;
            pipe->w_offset = 0;
        }
    }
    raw_spin_give(&pipe->lock);
    return backup_len;
}