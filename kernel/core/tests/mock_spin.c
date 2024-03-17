#include <spin.h>

void raw_spin_take(UNUSED spin_t *lock) {
}

void raw_spin_give(UNUSED spin_t *lock) {
}

int irq_spin_take(UNUSED spin_t *lock) {
    return 0;
}

void irq_spin_give(UNUSED spin_t *lock, UNUSED int key) {
}
