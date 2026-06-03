#ifndef ARCH_X86_64_DEV_BGA_H
#define ARCH_X86_64_DEV_BGA_H

#include <wheel.h>

// BGA framebuffer 物理地址回退值（PCI BAR0 读取失败时使用）
#define BGA_FB_FALLBACK  0xe0000000UL
#define BGA_FB_SIZE      (16U << 20)

INIT_TEXT int bga_check();  // 探测 BGA
INIT_TEXT uint32_t bga_get_address();
INIT_TEXT int bga_config(uint32_t w, uint32_t h, uint32_t bpp, uint32_t vw, uint32_t vh);
INIT_TEXT void bga_disable();

void bga_set_offset(uint16_t x, uint16_t y);

#endif // ARCH_X86_64_DEV_BGA_H
