#ifndef ARCH_X86_64_LIBA_MMU_H
#define ARCH_X86_64_LIBA_MMU_H

#include <base.h>

#define MMU_KERNEL  1   // user code cannot access
#define MMU_RDONLY  2   // user code cannot write
#define MMU_NOEXEC  4   // all code cannot execute

extern usize mmu_ctx_get();
extern void  mmu_ctx_set(usize ctx);

extern usize mmu_ctx_create();
extern usize mmu_translate(usize ctx, usize va);
extern void  mmu_map(usize ctx, usize va, usize pa, usize n, u32 attr);
extern void  mmu_unmap(usize ctx, usize va, usize n);

// requires: page-array
extern __INIT void kernel_ctx_init();
extern __INIT void kernel_ctx_load();

#endif // ARCH_X86_64_LIBA_MMU_H
