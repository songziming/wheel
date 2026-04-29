#include "rwlock.h"
#include <arch_api.h>

// Slim-Reader-Writer lock，参考 Windows/ReactOS 实现的读写锁
// 每个线程都有自己的 pender struct，缓存友好

#define WRITER  (1U << 0)
#define READER  (1U << 1)
#define READER_MASK (~(unsigned)WRITER)

//-----------------------------------------------------------------------------
// wait list helpers — caller must hold rw->guard
//-----------------------------------------------------------------------------

static void push_waiter(rwlock_t *rw, rwlock_waiter_t *w) {
    w->next = NULL;
    w->woken = 0;
    if (rw->wait_tail) {
        rw->wait_tail->next = w;
    } else {
        rw->wait_head = w;
    }
    rw->wait_tail = w;
}

// Transfer lock ownership to the waiter(s) at the front of the wait list.
// Under guard: the lock_word reflects the previous holder's release.
static void wake_next(rwlock_t *rw) {
    rwlock_waiter_t *w = rw->wait_head;
    if (!w) return;

    if (w->writer) {
        rw->wait_head = w->next;
        if (!rw->wait_head) rw->wait_tail = NULL;
        atomic_store(&rw->lock_word, WRITER);
        atomic_store(&w->woken, 1);
    } else {
        int n = 0;
        do {
            n++;
            rw->wait_head = w->next;
            atomic_store(&w->woken, 1);
            w = rw->wait_head;
        } while (w && !w->writer);
        if (!rw->wait_head) rw->wait_tail = NULL;
        atomic_store(&rw->lock_word, n * READER);
    }
}

//-----------------------------------------------------------------------------
// writer acquire / release
//-----------------------------------------------------------------------------

void rwlock_take_write(rwlock_t *rw) {
    unsigned expected = 0;
    if (atomic_compare_exchange_strong(&rw->lock_word, &expected, WRITER))
        return;

    rwlock_waiter_t w = { .writer = 1 };
    raw_spin_take(&rw->guard);

    expected = 0;
    if (atomic_compare_exchange_strong(&rw->lock_word, &expected, WRITER)) {
        raw_spin_give(&rw->guard);
        return;
    }

    push_waiter(rw, &w);
    raw_spin_give(&rw->guard);

    while (!atomic_load(&w.woken))
        cpu_pause();
}

void rwlock_give_write(rwlock_t *rw) {
    raw_spin_take(&rw->guard);

    if (rw->wait_head) {
        wake_next(rw);
    } else {
        atomic_store(&rw->lock_word, 0);
    }

    raw_spin_give(&rw->guard);
}

int irqrwlock_take_write(rwlock_t *rw) {
    int key = cpu_int_lock();

    unsigned expected = 0;
    if (atomic_compare_exchange_strong(&rw->lock_word, &expected, WRITER))
        return key;

    rwlock_waiter_t w = { .writer = 1 };
    raw_spin_take(&rw->guard);

    expected = 0;
    if (atomic_compare_exchange_strong(&rw->lock_word, &expected, WRITER)) {
        raw_spin_give(&rw->guard);
        return key;
    }

    push_waiter(rw, &w);
    raw_spin_give(&rw->guard);

    while (!atomic_load(&w.woken)) {
        cpu_int_unlock(key);
        cpu_pause();
        key = cpu_int_lock();
    }
    return key;
}

void irqrwlock_give_write(rwlock_t *rw, int key) {
    rwlock_give_write(rw);
    cpu_int_unlock(key);
}

//-----------------------------------------------------------------------------
// reader acquire / release
//-----------------------------------------------------------------------------

void rwlock_take_read(rwlock_t *rw) {
    unsigned old = atomic_load(&rw->lock_word);
    do {
        if (old & WRITER) goto slow;
    } while (!atomic_compare_exchange_weak(&rw->lock_word, &old, old + READER));
    return;

slow:
    rwlock_waiter_t w = { .writer = 0 };
    raw_spin_take(&rw->guard);

    old = atomic_load(&rw->lock_word);
    if (!(old & WRITER)) {
        unsigned want = old + READER;
        if (atomic_compare_exchange_strong(&rw->lock_word, &old, want)) {
            raw_spin_give(&rw->guard);
            return;
        }
    }

    push_waiter(rw, &w);
    raw_spin_give(&rw->guard);

    while (!atomic_load(&w.woken))
        cpu_pause();
}

void rwlock_give_read(rwlock_t *rw) {
    unsigned old = atomic_fetch_sub(&rw->lock_word, READER);
    if (old != READER) return;

    raw_spin_take(&rw->guard);

    unsigned cur = atomic_load(&rw->lock_word);
    if (cur != 0) {
        // A fast-path writer or reader took ownership between our
        // fetch_sub and acquiring guard — nothing left to do.
        raw_spin_give(&rw->guard);
        return;
    }

    if (rw->wait_head) {
        wake_next(rw);
    }

    raw_spin_give(&rw->guard);
}

int irqrwlock_take_read(rwlock_t *rw) {
    int key = cpu_int_lock();

    unsigned old = atomic_load(&rw->lock_word);
    do {
        if (old & WRITER) goto irq_slow;
    } while (!atomic_compare_exchange_weak(&rw->lock_word, &old, old + READER));
    return key;

irq_slow:
    rwlock_waiter_t w = { .writer = 0 };
    raw_spin_take(&rw->guard);

    old = atomic_load(&rw->lock_word);
    if (!(old & WRITER)) {
        unsigned want = old + READER;
        if (atomic_compare_exchange_strong(&rw->lock_word, &old, want)) {
            raw_spin_give(&rw->guard);
            return key;
        }
    }

    push_waiter(rw, &w);
    raw_spin_give(&rw->guard);

    while (!atomic_load(&w.woken)) {
        cpu_int_unlock(key);
        cpu_pause();
        key = cpu_int_lock();
    }
    return key;
}

void irqrwlock_give_read(rwlock_t *rw, int key) {
    rwlock_give_read(rw);
    cpu_int_unlock(key);
}
