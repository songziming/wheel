// BGA (Bochs Graphics Adaptor) 驱动
// 可以让 OS 设置显示分辨率/色彩深度

// 最大分辨率 2560x1600
// 最大显存大小 16MB（映射的 framebuffer 大小）
// BPP 允许的取值：4, 8, 15, 16, 24, 32

// 严格来说，应该通说 PCI 读取 bar0，获得 framebuffer 映射地址
// 但我们知道 Bochs/QEMU 映射地址必然是 0xE0000000，所以硬编码
//
//
// 支持硬件滚屏：设置 virt_height > height 可启用，之后通过 bga_set_offset()
// 改变显示起始行，无需 memcpy 即可滚屏。
//
// 使用示例（在 sys_init() 中 framebuffer 解析失败时调用）：
//
//   #include <dev/bga.h>
//   #include <dev/framebuf.h>
//
//   bga_info_t bga;
//   // virt_height = 768 * 2，即可容纳两个屏幕的内容，实现硬件滚屏
//   if (!g_fgcolor && !bga_init(&bga, 1024, 768, BGA_BPP_32, 768 * 2)) {
//       g_fgcolor = 0x00ffffff;
//       framebuf_init(bga.yres, bga.xres, bga.pitch, (uint32_t)bga.fb_pa);
//   }
//
//   滚屏时调用（在 framebuf 滚屏逻辑中）：
//       bga_set_offset(0, y_offset);  // 无需 memcpy！
//
// mem_init() 完成后调用 bga_enable_wc() 返回新 VA，替换 direct map 地址。
// 内部使用 mmu_unmap + mmu_map 将 framebuffer 从 direct map (WB) 移至
// MMIO_WC_BASE (WC)，消除双映射冲突且获得写合并性能。

#include "bga.h"

#include <cpu/rw.h>
#include <arch_config.h>
#include <debug.h>
#include <arch_api.h>
#include <vmspace.h>


// BGA 通过两个 16-bit IO 端口通信
#define BGA_PORT_INDEX  0x01CE  // 索引寄存器（选择要操作的寄存器号）
#define BGA_PORT_DATA   0x01CF  // 数据寄存器（读写由索引寄存器选中的寄存器）

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



//-----------------------------------------------------------------------------
// BGA 寄存器 IO
//-----------------------------------------------------------------------------

static void bga_write(uint16_t index, uint16_t data) {
    out16(BGA_PORT_INDEX, index);
    out16(BGA_PORT_DATA, data);
}

static uint16_t bga_read(uint16_t index) {
    out16(BGA_PORT_INDEX, index);
    return in16(BGA_PORT_DATA);
}

//-----------------------------------------------------------------------------
// PAT 配置 — 为 WC 映射做准备
//-----------------------------------------------------------------------------

// PAT (Page Attribute Table) MSR — 用于配置 WC (Write-Combining)
#define IA32_PAT  0x0277

// PA4 位于 PAT MSR 的 bit 16-18，{PAT, PCD, PWT} = {1,0,0} = 4
// 将 PA4 设为 WC(0x01) 后，在 PTE/PDE 中设置 PAT 位即可获得 WC 属性
#define PAT_PA4_SHIFT  16ULL
#define PAT_WC          0x01ULL

static CONST int g_pat_ready = 0;

// 将 ID=4 的 PAT entry 设为 Write-Combined
static void bga_enable_pat() {
    if (g_pat_ready) {
        return;
    }
    uint64_t pat = read_msr(IA32_PAT);
    pat &= ~(7ULL << PAT_PA4_SHIFT);
    pat |= PAT_WC << PAT_PA4_SHIFT;
    write_msr(IA32_PAT, pat);
    g_pat_ready = 1;
}

//-----------------------------------------------------------------------------
// 初始化
//-----------------------------------------------------------------------------

// 读取 BGA ID 确认硬件存在，返回 1 表示存在
INIT_TEXT int bga_check() {
    uint16_t id = bga_read(BGA_INDEX_ID);
    if ((BGA_ID0 <= id) && (id <= BGA_ID5)) {
        logk("found BGA, version 0x%x\n", id);
        return 1;
    }
    return 0;
}

// 配置显示模式，返回 1 表示成功
INIT_TEXT int bga_config(uint32_t w, uint32_t h, uint32_t bpp, uint32_t vw, uint32_t vh) {
    // 检查参数，总大小不能超过 16MB
    if ((vw * vh * bpp / 8) > BGA_FB_SIZE) {
        return 0;
    }

    bga_write(BGA_INDEX_ENABLE, 0);  // 先禁用 VBE 扩展

    bga_write(BGA_INDEX_XRES, w);
    bga_write(BGA_INDEX_YRES, h);
    bga_write(BGA_INDEX_BPP, bpp);

    // 虚拟分辨率：期望大于物理分辨率，用于硬件滚屏（横纵都能滚动）
    bga_write(BGA_INDEX_VIRT_WIDTH, vw);
    bga_write(BGA_INDEX_VIRT_HEIGHT, vh);
    bga_write(BGA_INDEX_X_OFFSET, 0);
    bga_write(BGA_INDEX_Y_OFFSET, 0);

    bga_write(BGA_INDEX_ENABLE, BGA_ENABLED | BGA_LFB_ENABLED); // 重新打开 VBE
    return 1;
}

INIT_TEXT void bga_disable() {
    bga_write(BGA_INDEX_ENABLE, 0);
}

//-----------------------------------------------------------------------------
// 硬件滚屏
//-----------------------------------------------------------------------------

// 设置显示窗口在虚拟 framebuffer 中的偏移。
// 改变后硬件立即从新的起始位置扫描输出，无需 memcpy。
void bga_set_offset(uint16_t x, uint16_t y) {
    bga_write(BGA_INDEX_X_OFFSET, x);
    bga_write(BGA_INDEX_Y_OFFSET, y);
}






//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------

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

// 获取 framebuffer 虚拟地址（通过 direct map，仅 bga_enable_wc 调用前有效）。
static inline uint8_t *bga_fb_ptr(bga_info_t *info) {
    return (uint8_t*)(DIRECT_MAP_ADDR + info->fb_pa);
}


INIT_TEXT int bga_init(bga_info_t *info, uint32_t width, uint32_t height,
                       uint32_t bpp, uint32_t virt_height) {
    if (NULL == info) {
        return 1;
    }

    // 读取 BGA ID 确认硬件存在
    uint16_t id = bga_read(BGA_INDEX_ID);
    if (id < BGA_ID0 || id > BGA_ID5) {
        logk("bga: device not found (id=0x%x)\n", id);
        return 1;
    }
    logk("bga: found device, version 0x%x\n", id);

    // 读取当前显示模式
    info->id   = id;
    info->xres = bga_read(BGA_INDEX_XRES);
    info->yres = bga_read(BGA_INDEX_YRES);
    info->bpp  = bga_read(BGA_INDEX_BPP);

    // QEMU/Bochs 默认 framebuffer 物理地址（即 PCI BAR0 的值）
    info->fb_pa   = BGA_FB_ADDR;
    info->fb_size = BGA_FB_SIZE;

    // 如果指定了新分辨率，切换显示模式
    if (width && height && bpp) {
        bga_write(BGA_INDEX_ENABLE, 0);  // 先禁用 VBE 扩展

        bga_write(BGA_INDEX_XRES, width);
        bga_write(BGA_INDEX_YRES, height);
        bga_write(BGA_INDEX_BPP, bpp);

        // 虚拟分辨率：期望大于物理分辨率，用于硬件滚屏
        uint32_t vh = virt_height ? virt_height : height;
        bga_write(BGA_INDEX_VIRT_WIDTH, width);
        bga_write(BGA_INDEX_VIRT_HEIGHT, vh);

        bga_write(BGA_INDEX_ENABLE, BGA_ENABLED | BGA_LFB_ENABLED);

        info->xres = width;
        info->yres = height;
        info->bpp  = bpp;
        info->virt_height = vh;
    } else {
        // 读取当前的虚拟分辨率
        info->virt_height = bga_read(BGA_INDEX_VIRT_HEIGHT);
        if (!info->virt_height) {
            info->virt_height = info->yres;
        }
    }

    info->pitch = info->xres * (info->bpp / 8);

    bga_enable_pat();

    logk("bga: %ux%ux%u, virt_h=%u, pitch=%u, fb=0x%llx\n",
         info->xres, info->yres, info->bpp, info->virt_height, info->pitch,
         info->fb_pa);

    return 0;
}

//-----------------------------------------------------------------------------
// WC (Write-Combining) 映射
//-----------------------------------------------------------------------------

// 从 direct map 中 unmap framebuffer 范围，再在 MMIO_WC_BASE 区域新建 WC 映射。
// 必须在 write_cr3(g_kernel_vm.table) 之后调用。返回新的虚拟地址。
INIT_TEXT uint8_t *bga_enable_wc(bga_info_t *info) {
    bga_enable_pat();

    uint64_t fb_va  = DIRECT_MAP_ADDR + info->fb_pa;
    uint64_t fb_end = fb_va + info->fb_size;

    // 从 direct map 中移除
    mmu_unmap(g_kernel_vm.table, fb_va, fb_end);

    // 在 MMIO_WC_BASE 区域用 WC 属性重新映射
    uint8_t *wc_va = (uint8_t*)MMIO_WC_BASE;
    mmu_map(g_kernel_vm.table, (size_t)wc_va, (size_t)wc_va + info->fb_size,
            info->fb_pa, MMU_WRITE | MMU_WC);

    logk("bga: WC mapped [%p, %p) -> PA 0x%llx\n", wc_va, wc_va + info->fb_size, info->fb_pa);
    return wc_va;
}
