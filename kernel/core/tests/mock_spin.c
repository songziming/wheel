#include <spin.h>

void raw_spin_take(spin_t *lock) {
    (void)lock;
}

void raw_spin_give(spin_t *lock) {
    (void)lock;
}

int irq_spin_take(spin_t *lock) {
    (void)lock;
    return 0;
}

void irq_spin_give(spin_t *lock, int key) {
    (void)lock;
    (void)key;
}
