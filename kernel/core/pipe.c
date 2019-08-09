#include <wheel.h>

typedef struct pipe_dev {
    iodev_t  dev;
    spin_t   spin;
    pfn_t    page;
    fifo_t   fifo;
    dllist_t r_penders;
    dllist_t w_penders;
} pipe_dev_t;

typedef struct pender {
    dlnode_t dl;
    task_t * tid;
} pender_t;

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
        // usize ret = pipe_dev_read(pipe, buf, len);
        usize ret = fifo_read(&pipe->fifo, buf, len);
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
        // usize ret = pipe_dev_write(pipe, buf, len);
        usize ret = fifo_write(&pipe->fifo, buf, len, NO);
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

// destructor function
static void pipe_dev_destroy(iodev_t * dev) {
    pipe_dev_t * pipe = (pipe_dev_t *) dev;
    page_block_free(pipe->page, 0);
    kmem_free(sizeof(pipe_dev_t), pipe);
}

iodev_t * pipe_dev_create() {
    pfn_t page = page_block_alloc(ZONE_NORMAL|ZONE_DMA, 0);
    if (NO_PAGE == page) {
        return NULL;
    }

    page_array[page].block = 1;
    page_array[page].order = 0;
    page_array[page].type  = PT_PIPE;

    pipe_dev_t * pipe = kmem_alloc(sizeof(pipe_dev_t));

    pipe->dev.ref   = 1;
    pipe->dev.free  = pipe_dev_destroy;
    pipe->dev.drv   = &pipe_drv;

    pipe->spin      = SPIN_INIT;
    pipe->page      = page;
    pipe->fifo      = FIFO_INIT(phys_to_virt((usize) page << PAGE_SHIFT), PAGE_SIZE);
    pipe->r_penders = DLLIST_INIT;
    pipe->w_penders = DLLIST_INIT;

    return (iodev_t *) pipe;
}
