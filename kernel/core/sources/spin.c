#include <wheel.h>


typedef struct spin {
    uint32_t ticket_counter;
    uint32_t service_counter;
} spin_t;


void raw_spin_take(spin_t *lock) {
    uint32_t ticket = atomic32_inc(&lock->ticket_counter);
    while (atomic32_get(&lock->service_counter) != ticket) {
        cpu_pause();
    }
}

void raw_spin_give(spin_t *lock) {
    atomic32_inc(&lock->service_counter);
}
