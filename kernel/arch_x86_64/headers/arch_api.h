// 封装平台细节，对外提供统一的接口
// 其他模块通过本文件使用平台功能

#ifndef ARCH_API_H
#define ARCH_API_H

#include <base.h>
#include <arch_config.h>

static inline void cpu_halt() { __asm__("hlt"); }
static inline void cpu_pause() { __asm__("pause"); }
static inline void cpu_rfence() { __asm__("lfence" ::: "memory"); }
static inline void cpu_wfence() { __asm__("sfence" ::: "memory"); }
static inline void cpu_rwfence() { __asm__("mfence" ::: "memory"); }

#endif // ARCH_API_H
