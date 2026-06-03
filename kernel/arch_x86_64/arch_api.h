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
// debug helper
//------------------------------------------------------------------------------

int arch_unwind_from(size_t *addrs, int max, uint64_t rbp);
int arch_unwind(size_t *addrs, int max);


#endif // ARCH_X86_64_ARCH_API_H
