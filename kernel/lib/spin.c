#include "spin.h"
#include <arch_api.h>
#include <kstring.h>

void spin_init(spin_t *spin) {
    kmemset(spin, 0, sizeof(spin_t));
}

void raw_spin_take(spin_t *spin) {
    atomic_uint ticket = atomic_fetch_add(&spin->ticket_counter, 1);
    while (atomic_load(&spin->service_counter) != ticket) {
        cpu_pause();
    }
}

void raw_spin_give(spin_t *spin) {
    atomic_fetch_add(&spin->service_counter, 1);
}

int irq_spin_take(spin_t *spin) {
    int key = cpu_int_lock();
    atomic_uint tickket = atomic_fetch_add(&spin->ticket_counter, 1);
    while (atomic_load(&spin->service_counter) != tickket) {
        cpu_int_unlock(key);
        cpu_pause();
        key = cpu_int_lock();
    }
    return key;
}

void irq_spin_give(spin_t *spin, int key) {
    atomic_fetch_add(&spin->service_counter, 1);
    cpu_int_unlock(key);
}
