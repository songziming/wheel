#include <arch_api.h>

uint32_t atomic32_get(volatile uint32_t *ptr) {
    return *ptr;
}

uint32_t atomic32_get_add(volatile uint32_t *ptr, uint32_t val) {
    uint32_t old = *ptr;
    *ptr += val;
    return old;
}
