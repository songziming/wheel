#ifndef CORE_SPIN_H
#define CORE_SPIN_H

#include <base.h>

typedef struct spin {
    int tkt;    // ticket counter
    int svc;    // service counter
} spin_t;

#define SPIN_INIT ((spin_t) { 0, 0 })

extern void raw_spin_take(spin_t * lock);
extern void raw_spin_give(spin_t * lock);

extern u32  irq_spin_take(spin_t * lock);
extern void irq_spin_give(spin_t * lock, u32 key);

#endif // CORE_SPIN_H
