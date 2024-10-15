#include <stdint.h>
#include <stdbool.h>

// 实际上本文件不是 mock，因为并未替代现有实现
// 只是因为我们没有编译汇编文件，缺失这些符号
// 更像是 dummy_arch

extern "C" {

// layout.ld
char _init_end;
char _text_addr, _text_end;
char _rodata_addr;
char _data_addr;
char _percpu_addr, _percpu_data_end, _percpu_bss_end;

// arch_entries.S
uint64_t isr_entries[1];
void task_entry() {}
void syscall_entry() {}
void arch_task_switch() {}


void load_gdtr() {}
void load_idtr() {}
void load_tr() {}

uint8_t atomic8_add(uint8_t *ptr, uint8_t val) {
    return __atomic_fetch_add(ptr, val, __ATOMIC_RELAXED);
}

uint32_t atomic32_get(uint32_t *ptr) {
    return __atomic_load_n(ptr, __ATOMIC_RELAXED);
}
uint32_t atomic32_set(uint32_t *ptr, uint32_t val) {
    return __atomic_exchange_n(ptr, val, __ATOMIC_RELAXED);
}
uint32_t atomic32_add(uint32_t *ptr, uint32_t val) {
    return __atomic_fetch_add(ptr, val, __ATOMIC_RELAXED);
}
uint32_t atomic32_cas(uint32_t *ptr, uint32_t cmp, uint32_t val) {
    uint32_t expected = cmp;
    __atomic_compare_exchange_n(ptr, &expected, val, false, __ATOMIC_RELAXED, __ATOMIC_RELAXED);
    return expected;
}

uint64_t atomic_set(uint64_t *ptr, uint64_t val) {
    return __atomic_exchange_n(ptr, val, __ATOMIC_RELAXED);
}
uint64_t atomic_cas(uint64_t *ptr, uint64_t cmp, uint64_t val) {
    uint64_t expected = cmp;
    __atomic_compare_exchange_n(ptr, &expected, val, false, __ATOMIC_RELAXED, __ATOMIC_RELAXED);
    return expected;
}
uint64_t atomic64_cas(uint64_t *ptr, uint64_t cmp, uint64_t val) __attribute__((alias("atomic_cas")));

}
