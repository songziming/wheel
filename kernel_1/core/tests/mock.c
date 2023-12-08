// 某些测试项需要 arch 提供一些函数
// 这里提供简单实现，能用即可

#include <arch_interface.h>
#include <stdlib.h>
#include <stdio.h>

#include <spin.h>


int irq_spin_take(spin_t *lock) {
    (void)lock;
    return 0;
}

void irq_spin_give(spin_t *lock, int key) {
    (void)lock;
    (void)key;
}

void spin_take(spin_t *lock) {
    (void)lock;
}

void spin_give(spin_t *lock) {
    (void)lock;
}


void *this_ptr(void *ptr) {
    return ptr;
}


void *early_alloc(size_t size) {
    return malloc(size);
}

void *early_const_alloc(size_t size) {
    return malloc(size);
}


PRINTF(1, 2) void dbg_print(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vfprintf(stdout, fmt, args);
    va_end(args);
}
