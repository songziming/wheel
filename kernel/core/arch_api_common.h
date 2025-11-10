#ifndef ARCH_API_COMMON_H
#define ARCH_API_COMMON_H

// 需要 arch 实现的函数

// atomic operations
uint8_t atomic8_fetch_add(volatile uint8_t *ptr, uint8_t val);
uint16_t atomic16_fetch_add(volatile uint16_t *ptr, uint16_t val);

void atomic32_or(volatile uint32_t *ptr, uint32_t val);
uint32_t atomic32_fetch(volatile uint32_t *ptr);
uint32_t atomic32_fetch_set(volatile uint32_t *ptr, uint32_t val);
uint32_t atomic32_fetch_add(volatile uint32_t *ptr, uint32_t val);
uint32_t atomic32_cas(volatile uint32_t *ptr, uint32_t cmp, uint32_t val);

uint64_t atomic64_fetch(volatile uint64_t *ptr);
uint64_t atomic64_fetch_set(volatile uint64_t *ptr, uint64_t val);
uint64_t atomic64_fetch_add(volatile uint64_t *ptr, uint32_t val);
uint64_t atomic64_cas(volatile uint64_t *ptr, uint64_t cmp, uint64_t val);

size_t atomic_fetch(volatile size_t *ptr);
size_t atomic_fetch_set(volatile size_t *ptr, size_t val);
size_t atomic_fetch_add(volatile size_t *ptr, size_t val);
size_t atomic_cas(volatile size_t *ptr, size_t cmp, size_t val);

#endif // ARCH_API_COMMON_H
