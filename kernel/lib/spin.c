#include "spin.h"
#include <arch_api.h>
#include <kstring.h>

void spin_init(spin_t *spin) {
    kmemset(spin, 0, sizeof(spin_t));
}

void raw_spin_take(spin_t *spin) {
    uint32_t ticket = atomic32_get_add(&spin->ticket_counter, 1);
    while (atomic32_get(&spin->service_counter) != ticket) {
        cpu_pause();
    }
}

void raw_spin_give(spin_t *spin) {
    atomic32_get_add(&spin->service_counter, 1);
}

int irq_spin_take(spin_t *spin) {
    int key = cpu_int_lock();
    uint32_t tickket = atomic32_get_add(&spin->ticket_counter, 1);
    while (atomic32_get(&spin->service_counter) != tickket) {
        cpu_int_unlock(key);
        cpu_pause();
        key = cpu_int_lock();
    }
    return key;
}

void irq_spin_give(spin_t *spin, int key) {
    atomic32_get_add(&spin->service_counter, 1);
    cpu_int_unlock(key);
}
