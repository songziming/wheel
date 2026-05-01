#ifndef ARCH_X86_64_DEV_BGA_H
#define ARCH_X86_64_DEV_BGA_H

#include <wheel.h>
#include <arch_config.h>

// BGA 寄存器索引（通过 0x01CE/0x01CF 端口访问）
#define BGA_INDEX_ID            0
#define BGA_INDEX_XRES          1
#define BGA_INDEX_YRES          2
#define BGA_INDEX_BPP           3
#define BGA_INDEX_ENABLE        4
#define BGA_INDEX_BANK          5
#define BGA_INDEX_VIRT_WIDTH    6
#define BGA_INDEX_VIRT_HEIGHT   7
#define BGA_INDEX_X_OFFSET      8
#define BGA_INDEX_Y_OFFSET      9

// BGA 版本 ID（从 VBE_INDEX_ID 寄存器读出）
#define BGA_ID0  0xB0C0
#define BGA_ID1  0xB0C1
#define BGA_ID2  0xB0C2
#define BGA_ID3  0xB0C3
#define BGA_ID4  0xB0C4
#define BGA_ID5  0xB0C5

// BGA BPP 寄存器值
#define BGA_BPP_4   0x04
#define BGA_BPP_8   0x08
#define BGA_BPP_15  0x0F
#define BGA_BPP_16  0x10
#define BGA_BPP_24  0x18
#define BGA_BPP_32  0x20

// BGA ENABLE 寄存器 flags
#define BGA_DISABLED      0x00
#define BGA_ENABLED       0x01
#define BGA_LFB_ENABLED   0x40
#define BGA_NOCLEARMEM    0x80

// BGA 默认 framebuffer 物理地址（QEMU/Bochs 默认值，PCI BAR0 也指向此处）
#define BGA_DEFAULT_FB_ADDR      0xE0000000
#define BGA_VIDEO_MEMORY_MB      16
#define BGA_VIDEO_MEMORY_BYTES   (BGA_VIDEO_MEMORY_MB * 1024 * 1024)

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

// 探测 BGA 并设置显示模式。
// virt_height 为虚拟高度（像素），设为大于 height 的值可启用硬件滚屏。
// 设为 0 表示等于 height（不启用滚屏）。
// width/height/bpp 为 0 表示保持当前模式不变。
// 成功时填充 info 并返回 0；未找到 BGA 则返回 1。
INIT_TEXT int bga_init(bga_info_t *info, uint32_t width, uint32_t height,
                       uint32_t bpp, uint32_t virt_height);

// 获取 framebuffer 虚拟地址（通过 direct map）。
static inline uint8_t *bga_fb_ptr(bga_info_t *info) {
    return (uint8_t*)(DIRECT_MAP_ADDR + info->fb_pa);
}

// 设置显示窗口在虚拟 framebuffer 中的偏移（硬件滚屏）。
// y 不超过 virt_height - yres。
void bga_set_offset(uint16_t x, uint16_t y);

// 将 framebuffer 对应的 direct map 区域设为 WC (Write-Combining)，
// 使显存写入能合并为突发传输，大幅提升绘图性能。
// 必须在 write_cr3(g_kernel_vm.table) 后调用（mem_init 完成之后）。
INIT_TEXT void bga_enable_wc(bga_info_t *info);

#endif // ARCH_X86_64_DEV_BGA_H
