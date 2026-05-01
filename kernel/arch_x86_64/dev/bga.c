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
// 另外，mem_init() 完成后调用 bga_enable_wc() 可以启用 WC 写合并，加速绘图。

#include "bga.h"

#include <cpu/rw.h>
#include <arch_config.h>
#include <debug.h>
#include <page.h>
#include <kstring.h>
#include <vmspace.h>


// BGA 通过两个 16-bit IO 端口通信
#define BGA_PORT_INDEX  0x01CE  // 索引寄存器（选择要操作的寄存器号）
#define BGA_PORT_DATA   0x01CF  // 数据寄存器（读写由索引寄存器选中的寄存器）

// PAT (Page Attribute Table) MSR — 用于配置 WC (Write-Combining)
#define IA32_PAT  0x0277

// PA4 位于 PAT MSR 的 bit 16-18，{PAT, PCD, PWT} = {1,0,0} = 4
// 将 PA4 设为 WC(0x01) 后，在 PDE 中设置 PAT 位即可获得 WC 属性
#define PAT_PA4_SHIFT  16ULL
#define PAT_WC          0x01ULL

// 页表项位标志（硬件定义，与 mmu.c 保持一致）
#define PT_NX       0x8000000000000000ULL
#define PT_ADDR     0x000ffffffffff000ULL
#define PT_PS       0x0000000000000080ULL
#define PT_P        0x0000000000000001ULL
#define PT_PAT_2M   0x0000000000001000ULL  // PAT for 2M PDE / 1G PDPE

#define SZ_2M  (1ULL << 21)
#define SZ_4K  (1ULL << 12)

static CONST int g_pat_ready = 0;

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
    info->fb_pa   = BGA_DEFAULT_FB_ADDR;
    info->fb_size = BGA_VIDEO_MEMORY_BYTES;

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
// 硬件滚屏
//-----------------------------------------------------------------------------

// 设置显示窗口在虚拟 framebuffer 中的偏移。
// 改变后硬件立即从新的起始位置扫描输出，无需 memcpy。
void bga_set_offset(uint16_t x, uint16_t y) {
    bga_write(BGA_INDEX_X_OFFSET, x);
    bga_write(BGA_INDEX_Y_OFFSET, y);
}

//-----------------------------------------------------------------------------
// WC (Write-Combining) 映射
//-----------------------------------------------------------------------------

// 将 direct map 中覆盖 [va, end) 的 2M PDE 的 PAT 位打开，启用 WC 写合并。
// 如果 direct map 使用 1G 大页，会先拆分成 2M 页再设置 PAT 位。
INIT_TEXT void bga_enable_wc(bga_info_t *info) {
    bga_enable_pat();

    uint64_t fb_va  = DIRECT_MAP_ADDR + info->fb_pa;
    uint64_t fb_end = DIRECT_MAP_ADDR + info->fb_pa + info->fb_size;

    // 对齐到 2M 边界
    uint64_t va  = fb_va & ~(SZ_2M - 1);
    uint64_t end = (fb_end + SZ_2M - 1) & ~(SZ_2M - 1);

    uint64_t pml4_pa = g_kernel_vm.table;
    uint64_t *pml4 = (uint64_t*)(DIRECT_MAP_ADDR + pml4_pa);

    for (; va < end; va += SZ_2M) {
        int pml4_idx = (va >> 39) & 0x1ff;
        if (!(pml4[pml4_idx] & PT_P)) {
            logk("bga: PML4[%d] not present, cannot set WC\n", pml4_idx);
            return;
        }

        uint64_t *pdp = (uint64_t*)(DIRECT_MAP_ADDR + (pml4[pml4_idx] & PT_ADDR));
        int pdp_idx = (va >> 30) & 0x1ff;
        if (!(pdp[pdp_idx] & PT_P)) {
            logk("bga: PDP[%d] not present, cannot set WC\n", pdp_idx);
            return;
        }

        // 如果 direct map 此处是 1G 大页，先拆成 2M 页
        if (pdp[pdp_idx] & PT_PS) {
            uint64_t pa_1g = pdp[pdp_idx] & PT_ADDR;
            uint64_t attrs = pdp[pdp_idx] & ~(PT_ADDR | PT_PS);

            uint64_t new_pd = PAGE_ALLOC(0, PT_PGTBL);
            if (!new_pd) {
                logk("bga: failed to alloc PD for 1G split\n");
                return;
            }
            kmemset((void*)(DIRECT_MAP_ADDR + new_pd), 0, SZ_4K);

            uint64_t *pd = (uint64_t*)(DIRECT_MAP_ADDR + new_pd);
            for (int i = 0; i < 512; ++i) {
                pd[i] = (pa_1g + i * SZ_2M) | PT_P | PT_PS | attrs;
            }

            // 将 PDPE 改为指向 PD 的非叶条目
            pdp[pdp_idx] = new_pd | PT_P | 0x6;  // US+RW，权限由末级 PDE 决定
        }

        uint64_t *pd = (uint64_t*)(DIRECT_MAP_ADDR + (pdp[pdp_idx] & PT_ADDR));
        int pd_idx = (va >> 21) & 0x1ff;

        if (pd[pd_idx] & PT_P) {
            pd[pd_idx] |= PT_PAT_2M;
            ASMV("invlpg (%0)" :: "r"(va) : "memory");
        }
    }

    logk("bga: WC enabled for fb [0x%llx, 0x%llx)\n", fb_va, fb_end);
}
