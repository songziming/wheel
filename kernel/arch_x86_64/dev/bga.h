#ifndef ARCH_X86_64_DEV_BGA_H
#define ARCH_X86_64_DEV_BGA_H

#include <wheel.h>
#include <arch_config.h>

// BGA 默认 framebuffer 物理地址（QEMU/Bochs 默认值，PCI BAR0 也指向此处）
#define BGA_FB_ADDR     0xe0000000
#define BGA_FB_SIZE     (16U << 20)

typedef struct bga_info {
    uint16_t id;          // BGA 版本 ID（BGA_ID0 ~ BGA_ID5）
    uint32_t xres;        // 水平分辨率（物理可见）
    uint32_t yres;        // 垂直分辨率（物理可见）
    uint32_t bpp;         // 每像素位数（BGA_BPP_*）
    uint32_t pitch;       // 一行字节数（= xres * bpp/8）
    uint32_t virt_height; // 虚拟高度（像素），>= yres，用于硬件滚屏（0 表示 = yres）
    uint64_t fb_pa;       // framebuffer 物理地址
    uint64_t fb_size;     // framebuffer 大小（字节）
} bga_info_t;


INIT_TEXT int bga_check();  // 探测 BGA
INIT_TEXT int bga_config(uint32_t w, uint32_t h, uint32_t bpp, uint32_t vw, uint32_t vh);
INIT_TEXT void bga_disable();

// 探测 BGA 并设置显示模式。
// virt_height 为虚拟高度（像素），设为大于 height 的值可启用硬件滚屏。
// 设为 0 表示等于 height（不启用滚屏）。
// width/height/bpp 为 0 表示保持当前模式不变。
// 成功时填充 info 并返回 0；未找到 BGA 则返回 1。
INIT_TEXT int bga_init(bga_info_t *info, uint32_t width, uint32_t height,
                       uint32_t bpp, uint32_t virt_height);

// 获取 framebuffer 虚拟地址（通过 direct map，仅 bga_enable_wc 调用前有效）。
static inline uint8_t *bga_fb_ptr(bga_info_t *info) {
    return (uint8_t*)(DIRECT_MAP_ADDR + info->fb_pa);
}

// 设置显示窗口在虚拟 framebuffer 中的偏移（硬件滚屏）。
// y 不超过 virt_height - yres。
void bga_set_offset(uint16_t x, uint16_t y);

// 先 unmap direct map 中的 framebuffer 范围，再在 MMIO_WC_BASE
// 区域以 WC 属性重新映射。必须在 write_cr3(g_kernel_vm.table) 后调用。
// 返回新的虚拟地址，后续绘图应使用此地址。
INIT_TEXT uint8_t *bga_enable_wc(bga_info_t *info);

#endif // ARCH_X86_64_DEV_BGA_H
