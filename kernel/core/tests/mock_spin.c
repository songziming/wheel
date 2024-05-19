#include <spin.h>

void raw_spin_take(spin_t *lock UNUSED) {
}

void raw_spin_give(spin_t *lock UNUSED) {
}

int irq_spin_take(spin_t *lock UNUSED) {
    return 0;
}

void irq_spin_give(spin_t *lock UNUSED, int key UNUSED) {
}
