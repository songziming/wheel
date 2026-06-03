#ifndef SPIN_H
#define SPIN_H

#include <wheel.h>
#include "lockdep.h"

typedef struct spin {
    _Atomic uint32_t ticket_counter;
    _Atomic uint32_t service_counter;
    lockdep_instance_t dep;
} spin_t;
#define SPIN_INIT (spin_t){0,0,{.file=__FILE__,.line=__LINE__}}

void raw_spin_take(spin_t *spin);
void raw_spin_give(spin_t *spin);
int  irq_spin_take(spin_t *spin);
void irq_spin_give(spin_t *spin, int key);


typedef struct rwspin {
    spin_t spin;
    _Atomic uint32_t reader_num;
    lockdep_instance_t dep;
} rwspin_t;
#define RWSPIN_INIT (rwspin_t){.spin = {0,0,{NULL,0}}, .reader_num = 0, .dep = {.file = __FILE__, .line = __LINE__}}

void rwspin_take_writer(rwspin_t *rw);
void rwspin_give_writer(rwspin_t *rw);
void rwspin_take_reader(rwspin_t *rw);
void rwspin_give_reader(rwspin_t *rw);
int  irqrw_take_writer(rwspin_t *rw);
int  irqrw_take_reader(rwspin_t *rw);
void irqrw_give_writer(rwspin_t *rw, int key);
void irqrw_give_reader(rwspin_t *rw, int key);

#endif // SPIN_H
