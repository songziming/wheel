// 消息队列，阻塞式单向数据通道

#include <wheel.h>
#include <fifo.h>


typedef struct pipe {
    spin_t  spin;
    fifo_t  fifo;

    dlnode_t    writers;
    dlnode_t    readers;
} pipe_t;


typedef struct pender {
    dlnode_t dl;
    task_t  *tid;
} pender_t;


static void unpend_all(dlnode_t *q) {
    // dlnode_t *node = q->next;
    while (!dl_is_lastone(q)) {
        pender_t *pender = containerof(q->next, pender_t, dl);
        dl_remove(&pender->dl);
        task_resume(pender->tid);
    }
}


void pipe_read(pipe_t *pipe, void *dst, size_t len) {
    unpend_all(&pipe->writers);
    (void)dst;
    (void)len;
}

void pipe_write(pipe_t *pipe, const void *src, size_t len) {
    unpend_all(&pipe->readers);
    (void)src;
    (void)len;
}
