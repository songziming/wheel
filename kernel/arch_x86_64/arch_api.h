#ifndef ARCH_X86_64_ARCH_API_H
#define ARCH_X86_64_ARCH_API_H

#include <wheel.h>
#include <arch_api_common.h>
#include "arch_config.h"
#include "cpu/rw.h"
#include "mem/thiscpu_rw.h"


//------------------------------------------------------------------------------
// inline assembly helpers
//------------------------------------------------------------------------------

static inline void cpu_halt() { ASMV("hlt"); }
static inline void cpu_pause() { ASMV("pause"); }
static inline void cpu_rfence() { ASMV("lfence" ::: "memory"); }
static inline void cpu_wfence() { ASMV("sfence" ::: "memory"); }
static inline void cpu_rwfence() { ASMV("mfence" ::: "memory"); }

//------------------------------------------------------------------------------
// identity-map 物理地址转换
//------------------------------------------------------------------------------

// 将物理地址转换为 identity map 区域的虚拟地址
// 单元测试中重定向到 mmap 的内存
#ifdef UNIT_TEST
extern uint64_t g_idmap_base;
static inline char *idmap_at(size_t pa) {
    return (char*)(g_idmap_base + pa);
}
#else
static inline char *idmap_at(size_t pa) {
    return (char*)(IDENTITY_MAP_ADDR + pa);
}
#endif

//------------------------------------------------------------------------------
// debug helper
//------------------------------------------------------------------------------

int arch_unwind_from(size_t *addrs, int max, uint64_t rbp);
int arch_unwind(size_t *addrs, int max);

//------------------------------------------------------------------------------
// PCI(e) support
//------------------------------------------------------------------------------

INIT_TEXT void arch_pci_init();

#endif // ARCH_X86_64_ARCH_API_H
