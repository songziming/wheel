#ifndef ARCH_API_COMMON_H
#define ARCH_API_COMMON_H

#include <stddef.h>
#include <stdint.h>

// 需要 arch 实现的函数

// // atomic operations
// uint8_t  atomic8_get_add(volatile uint8_t *ptr, uint8_t val);
// uint16_t atomic16_get_add(volatile uint16_t *ptr, uint16_t val);

// uint32_t atomic32_get(volatile uint32_t *ptr);
// uint32_t atomic32_get_set(volatile uint32_t *ptr, uint32_t val);
// uint32_t atomic32_get_add(volatile uint32_t *ptr, uint32_t val);
// void     atomic32_get_or (volatile uint32_t *ptr, uint32_t val);
// uint32_t atomic32_cmp_set(volatile uint32_t *ptr, uint32_t cmp, uint32_t val);

// uint64_t atomic64_get(volatile uint64_t *ptr);
// uint64_t atomic64_get_set(volatile uint64_t *ptr, uint64_t val);
// uint64_t atomic64_get_add(volatile uint64_t *ptr, uint32_t val);
// uint64_t atomic64_cmp_set(volatile uint64_t *ptr, uint64_t cmp, uint64_t val);

// size_t atomic_get(volatile size_t *ptr);
// size_t atomic_get_set(volatile size_t *ptr, size_t val);
// size_t atomic_get_add(volatile size_t *ptr, size_t val);
// size_t atomic_cmp_set(volatile size_t *ptr, size_t cmp, size_t val);

#endif // ARCH_API_COMMON_H
