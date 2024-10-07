#ifndef SPIN_H
#define SPIN_H

#include <common.h>

typedef struct spin {
    volatile int32_t ticket_counter;
    volatile int32_t service_counter;
} spin_t;

void spin_init(spin_t *lock);
void raw_spin_take(spin_t *lock);
void raw_spin_give(spin_t *lock);
int  irq_spin_take(spin_t *lock);
void irq_spin_give(spin_t *lock, int key);

#endif // SPIN_H
