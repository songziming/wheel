#include <spin.h>
#include <arch_interface.h>
#include <debug.h>

void spin_take(spin_t *lock) {
    ASSERT(0 == get_int_depth());

    uint32_t ticket = atomic32_add(&lock->ticket, 1);
    cpu_rfence();
    while (ticket != lock->service) {
        cpu_pause();
    }
}

void spin_give(spin_t *lock) {
    ASSERT(0 == get_int_depth());

    atomic32_add(&lock->service, 1);
}

int irq_spin_take(spin_t *lock) {
    int key = cpu_int_lock();
    uint32_t ticket = atomic32_add(&lock->ticket, 1);
    cpu_rfence();
    while (ticket != lock->service) {
        cpu_int_unlock(key);
        cpu_pause();
        key = cpu_int_lock();
    }
    return key;
}

void irq_spin_give(spin_t *lock, int key) {
    atomic32_add(&lock->service, 1);
    cpu_int_unlock(key);
}
