#include <wheel.h>

#if (CFG_KBD_BUFF_SIZE & (CFG_KBD_BUFF_SIZE - 1)) != 0
    #error "CFG_KBD_BUFF_SIZE must be power of 2"
#endif

// TODO: change this file into `event.c`, so that all kinds of input can
//       share the same interface (keyboard, mouse, timer, etc).
//       using a uniform message queue is very important for GUI.

typedef struct pender {
    dlnode_t dl;
    task_t * tid;
} pender_t;

// singleton fifo buffer, non blocking write, blocking read
static keycode_t kbd_buff[CFG_KBD_BUFF_SIZE];
static spin_t    kbd_spin    = SPIN_INIT;
static dllist_t  kbd_penders = DLLIST_INIT;
static fifo_t    kbd_fifo    = FIFO_INIT(kbd_buff, CFG_KBD_BUFF_SIZE * sizeof(keycode_t));

static void flush_penders() {
    while (1) {
        dlnode_t * head = dl_pop_head(&kbd_penders);
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

// non blocking write, called in driver ISR
void kbd_send(keycode_t code) {
    u32 key = irq_spin_take(&kbd_spin);
    fifo_write(&kbd_fifo, (u8 *) &code, sizeof(keycode_t), NO);

    flush_penders();
    irq_spin_give(&kbd_spin, key);
    task_switch();
}

// non blocking read
keycode_t kbd_peek() {
    static keycode_t code;

    u32 key = irq_spin_take(&kbd_spin);
    usize len = fifo_peek(&kbd_fifo, (u8 *) &code, sizeof(keycode_t));
    irq_spin_give(&kbd_spin, key);

    if (0 != len) {
        return code;
    } else {
        return KEY_RESERVED;
    }
}

// blocking read
keycode_t kbd_recv() {
    static keycode_t code;
    assert(0 == thiscpu_var(int_depth));

    while (1) {
        u32 key = irq_spin_take(&kbd_spin);
        if (0 != fifo_read(&kbd_fifo, (u8 *) &code, sizeof(keycode_t))) {
            irq_spin_give(&kbd_spin, key);
            return code;
        }

        // pend current task
        task_t * tid    = thiscpu_var(tid_prev);
        pender_t pender = {
            .dl  = DLNODE_INIT,
            .tid = tid,
        };
        dl_push_tail(&kbd_penders, &pender.dl);

        // pend here and try again
        raw_spin_take(&tid->spin);
        sched_stop(tid, TS_PEND);
        raw_spin_give(&tid->spin);
        irq_spin_give(&kbd_spin, key);
        task_switch();
    }
}
