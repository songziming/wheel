#include <spin.h>
#include <wheel.h>

void raw_spin_take(spin_t *lock) {
    uint32_t ticket = atomic32_inc(&lock->ticket_counter);
    while (atomic32_get(&lock->service_counter) != ticket) {
        cpu_pause();
    }
}

void raw_spin_give(spin_t *lock) {
    atomic32_inc(&lock->service_counter);
}

int irq_spin_take(spin_t *lock) {
    int key = cpu_int_lock();
    uint32_t tkt = atomic32_inc(&lock->ticket_counter);
    while (atomic32_get(&lock->service_counter) != tkt) {
        cpu_int_unlock(key);
        cpu_pause();
        key = cpu_int_lock();
    }
    return key;
}

void irq_spin_give(spin_t *lock, int key) {
    atomic32_inc(&lock->service_counter);
    cpu_int_unlock(key);
}
