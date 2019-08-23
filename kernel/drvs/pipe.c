#include <wheel.h>

typedef struct pipe {
    kref_t   ref;
    spin_t   spin;
    pfn_t    page;
    fifo_t   fifo;
    dllist_t r_penders;
    dllist_t w_penders;
} pipe_t;

typedef struct pender {
    dlnode_t dl;
    task_t * tid;
} pender_t;

//------------------------------------------------------------------------------
// pipe destructor and constructor

static void pipe_destroy(pipe_t * pipe) {
    page_block_free(pipe->page, 0);
    kmem_free(sizeof(pipe_t), pipe);
}

pipe_t * pipe_create() {
    pfn_t page = page_block_alloc(ZONE_NORMAL|ZONE_DMA, 0, PT_PIPE);
    if (NO_PAGE == page) {
        return NULL;
    }

    pipe_t * pipe = kmem_alloc(sizeof(pipe_t));
    pipe->ref       = KREF_INIT(pipe_destroy);
    pipe->spin      = SPIN_INIT;
    pipe->page      = page;
    pipe->fifo      = FIFO_INIT(phys_to_virt((usize) page << PAGE_SHIFT), PAGE_SIZE);
    pipe->r_penders = DLLIST_INIT;
    pipe->w_penders = DLLIST_INIT;

    return pipe;
}

//------------------------------------------------------------------------------
// blocking version of read write functions

static void unpend_all(dllist_t * list) {
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

usize pipe_read(pipe_t * pipe, u8 * buf, usize len) {
    while (1) {
        raw_spin_take(&pipe->spin);
        usize ret = fifo_read(&pipe->fifo, buf, len);
        if (0 != ret) {
            preempt_lock();
            unpend_all(&pipe->w_penders);

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

usize pipe_write(pipe_t * pipe, const u8 * buf, usize len) {
    while (1) {
        raw_spin_take(&pipe->spin);
        usize ret = fifo_write(&pipe->fifo, buf, len, NO);
        if (0 != ret) {
            preempt_lock();
            unpend_all(&pipe->r_penders);

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
// new file interface

static usize pipe_file_read(file_t * file, u8 * buf, usize len) {
    return pipe_read((pipe_t *) file->private, buf, len);
}

static usize pipe_file_write(file_t * file, const u8 * buf, usize len) {
    return pipe_write((pipe_t *) file->private, buf, len);
}

static const fops_t pipe_ops = {
    .read  = (read_t)  pipe_file_read,
    .write = (write_t) pipe_file_write,
    .lseek = (lseek_t) NULL,
};

static void pipe_file_delete(file_t * file) {
    kref_delete(file->private);
    kmem_free(sizeof(file_t), file);
}

file_t * pipe_file_create(pipe_t * pipe, int mode) {
    file_t * file  = kmem_alloc(sizeof(file_t));
    file->ref      = KREF_INIT(pipe_file_delete);
    file->ops_mode = ((usize) &pipe_ops & ~3UL) | (mode & 3);

    if (NULL != pipe) {
        file->private = kref_retain(pipe);
    } else {
        file->private = pipe_create();
    }

    return file;
}