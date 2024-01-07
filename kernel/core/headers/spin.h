#ifndef SPIN_H
#define SPIN_H

#include <def.h>

typedef struct spin {
    uint32_t ticket_counter;
    uint32_t service_counter;
} spin_t;

#define SPIN_INIT ((spin_t){ 0,0 })

void raw_spin_take(spin_t *lock);
void raw_spin_give(spin_t *lock);
int  irq_spin_take(spin_t *lock);
void irq_spin_give(spin_t *lock, int key);

#endif // SPIN_H
