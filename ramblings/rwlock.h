#ifndef RWLOCK_H
#define RWLOCK_H

#include <wheel.h>
#include <spin.h>

//-----------------------------------------------------------------------------
// Slim reader-writer lock — modelled on Windows SRWLock
//
// Single atomic lock word encodes state:
//   bit 0  — exclusive (writer held)
//   bits 1+ — reader count
//
// Waiters register on a FIFO wait list and spin locally (no blocking).
// A releasing holder wakes the next waiter(s):
//   - one writer, or
//   - all contiguous readers at the front of the list
//-----------------------------------------------------------------------------

typedef struct rwlock_waiter {
    struct rwlock_waiter *next;
    atomic_int             woken;   // waiter spins until this becomes 1
    int                    writer;  // 1 = waiting for write, 0 = waiting for read
} rwlock_waiter_t;

typedef struct rwlock {
    atomic_uint      lock_word;
    spin_t           guard;        // protects the wait list
    rwlock_waiter_t *wait_head;
    rwlock_waiter_t *wait_tail;
} rwlock_t;

#define RWLOCK_INIT ((rwlock_t){0,SPIN_INIT,NULL,NULL})

void rwlock_take_write(rwlock_t *rw);
void rwlock_give_write(rwlock_t *rw);
void rwlock_take_read(rwlock_t *rw);
void rwlock_give_read(rwlock_t *rw);

int  irqrwlock_take_write(rwlock_t *rw);
int  irqrwlock_take_read(rwlock_t *rw);
void irqrwlock_give_write(rwlock_t *rw, int key);
void irqrwlock_give_read(rwlock_t *rw, int key);

#endif // RWLOCK_H
