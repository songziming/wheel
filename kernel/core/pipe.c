#include <wheel.h>

typedef struct pipe_dev {
    iodev_t  dev;
    spin_t   spin;
    pglist_t pages;
    usize    r_offset;  // must within pages.head [0, PAGE_SIZE-1]
    usize    w_offset;  // must within pages.tail [0, PAGE_SIZE-1]
    dllist_t r_penders;
    dllist_t w_penders;
} pipe_dev_t;

typedef struct pender {
    dlnode_t dl;
    task_t * tid;
} pender_t;

//------------------------------------------------------------------------------
// read write function without pending

static usize pipe_dev_read(pipe_dev_t * pipe, u8 * buf, usize len) {
    usize backup_len = len;

    if ((pipe->pages.head  == pipe->pages.tail) &&
        (pipe->r_offset == pipe->w_offset)) {
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

    return backup_len;
}

static usize pipe_dev_write(pipe_dev_t * pipe, const u8 * buf, usize len) {
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

    return backup_len;
}

//------------------------------------------------------------------------------
// blocking version of read write functions

static void flush_penders(dllist_t * list) {
    while (1) {
        dlnode_t * head = dl_pop_head(list);
        if (NULL == head) {
            return;
        }

        pender_t * pender = PARENT(head, pender_t, dl);
        task_t   * tid    = pender->tid;

        raw_spin_take(&tid->spin);
        sched_cont(tid, TS_PEND);
        int cpu = tid->last_cpu;
        raw_spin_give(&tid->spin);

        if (cpu_index() != cpu) {
            smp_resched(cpu);
        }
    }
}

static usize pipe_read(iodev_t * dev, u8 * buf, usize len, usize * pos __UNUSED) {
    pipe_dev_t * pipe = (pipe_dev_t *) dev;

    while (1) {
        raw_spin_take(&pipe->spin);
        usize ret = pipe_dev_read(pipe, buf, len);
        if (0 != ret) {
            preempt_lock();
            flush_penders(&pipe->w_penders);
            raw_spin_give(&pipe->spin);
            preempt_unlock();

            task_switch();
            return ret;
        }

        // failed to read, pend current task
        task_t * tid    = thiscpu_var(tid_prev);
        pender_t pender = {
            .dl  = DLNODE_INIT,
            .tid = tid,
        };
        dl_push_tail(&pipe->r_penders, &pender.dl);

        // pend here and try again
        raw_spin_take(&tid->spin);
        sched_stop(tid, TS_PEND);
        raw_spin_give(&tid->spin);
        raw_spin_give(&pipe->spin);
        task_switch();
    }
}

static usize pipe_write(iodev_t * dev, const u8 * buf, usize len, usize * pos __UNUSED) {
    pipe_dev_t * pipe = (pipe_dev_t *) dev;

    while (1) {
        raw_spin_take(&pipe->spin);
        usize ret = pipe_dev_write(pipe, buf, len);
        if (0 != ret) {
            preempt_lock();
            flush_penders(&pipe->r_penders);
            raw_spin_give(&pipe->spin);
            preempt_unlock();

            task_switch();
            return ret;
        }

        // failed to write, pend current task
        task_t * tid    = thiscpu_var(tid_prev);
        pender_t pender = {
            .dl  = DLNODE_INIT,
            .tid = tid,
        };
        dl_push_tail(&pipe->w_penders, &pender.dl);

        // pend here and try again
        raw_spin_take(&tid->spin);
        sched_stop(tid, TS_PEND);
        raw_spin_give(&tid->spin);
        raw_spin_give(&pipe->spin);
        task_switch();
    }
}

//------------------------------------------------------------------------------
// pipe driver and device

static iodrv_t pipe_drv = {
    .read  = (ios_read_t)  pipe_read,
    .write = (ios_write_t) pipe_write,
    .lseek = (ios_lseek_t) NULL,
};

static void pipe_dev_destroy(iodev_t * dev) {
    pipe_dev_t * pipe = (pipe_dev_t *) dev;
    pglist_free_all(&pipe->pages);
    kmem_free(sizeof(pipe_dev_t), pipe);
}

iodev_t * pipe_dev_create() {
    pipe_dev_t * pipe = kmem_alloc(sizeof(pipe_dev_t));

    pipe->dev.ref   = 1;
    pipe->dev.free  = pipe_dev_destroy;
    pipe->dev.drv   = &pipe_drv;

    pipe->spin      = SPIN_INIT;
    pipe->pages     = PGLIST_INIT;
    pipe->r_offset  = 0;
    pipe->w_offset  = 0;
    pipe->r_penders = DLLIST_INIT;
    pipe->w_penders = DLLIST_INIT;

    pfn_t pn = page_block_alloc_or_fail(ZONE_NORMAL|ZONE_DMA, 0);
    page_array[pn].block = 1;
    page_array[pn].order = 0;
    page_array[pn].type  = PT_PIPE;
    pglist_push_tail(&pipe->pages, pn);

    return (iodev_t *) pipe;
}
