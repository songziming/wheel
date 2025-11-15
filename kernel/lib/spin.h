#ifndef LIB_SPIN_H
#define LIB_SPIN_H

#include <wheel.h>

typedef struct spin {
    atomic_uint ticket_counter;
    atomic_uint service_counter;
} spin_t;

void spin_init(spin_t *spin);
void raw_spin_take(spin_t *spin);
void raw_spin_give(spin_t *spin);
int  irq_spin_take(spin_t *spin);
void irq_spin_give(spin_t *spin, int key);

#endif // LIB_SPIN_H
