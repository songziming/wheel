#include "spin.h"
#include <arch_intf.h>


void spin_init(spin_t *lock) {
    lock->ticket_counter = 0;
    lock->service_counter = 0;
}

void raw_spin_take(spin_t *lock) {
    uint32_t ticket = atomic32_add(&lock->ticket_counter, 1);
    while (atomic32_get(&lock->service_counter) != ticket) {
        cpu_pause();
    }
}

void raw_spin_give(spin_t *lock) {
    atomic32_add(&lock->service_counter, 1);
}

int irq_spin_take(spin_t *lock) {
    int key = cpu_int_lock();
    uint32_t tickket = atomic32_add(&lock->ticket_counter, 1);
    while (atomic32_get(&lock->service_counter) != tickket) {
        cpu_int_unlock(key);
        cpu_pause();
        key = cpu_int_lock();
    }
    return key;
}

void irq_spin_give(spin_t *lock, int key) {
    atomic32_add(&lock->service_counter, 1);
    cpu_int_unlock(key);
}
