#ifndef SPIN_H
#define SPIN_H

#include <wheel.h>

typedef struct spin {
    atomic_uint ticket_counter;
    atomic_uint service_counter;
} spin_t;
#define SPIN_INIT (spin_t){0,0}

// void spin_init(spin_t *spin);
void raw_spin_take(spin_t *spin);
void raw_spin_give(spin_t *spin);
int  irq_spin_take(spin_t *spin);
void irq_spin_give(spin_t *spin, int key);


typedef struct rwspin {
    spin_t spin;
    atomic_uint reader_num;
} rwspin_t;
#define RWSPIN_INIT (rwspin_t){SPIN_INIT,0}

// void rwspin_init(rwspin_t *rw);
void rwspin_take_writer(rwspin_t *rw);
void rwspin_give_writer(rwspin_t *rw);
void rwspin_take_reader(rwspin_t *rw);
void rwspin_give_reader(rwspin_t *rw);
int  irqrw_take_writer(rwspin_t *rw);
int  irqrw_take_reader(rwspin_t *rw);
void irqrw_give_writer(rwspin_t *rw, int key);
void irqrw_give_reader(rwspin_t *rw, int key);

#endif // SPIN_H
