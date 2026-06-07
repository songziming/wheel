// BGA (Bochs Graphics Adaptor) 驱动
// 可以让 OS 设置显示分辨率/色彩深度

// 最大分辨率 2560x1600
// 最大显存大小 16MB（映射的 framebuffer 大小）
// BPP 允许的取值：4, 8, 15, 16, 24, 32

// qemu 可以通过 PCI BAR0 获取 framebuffer 物理地址
// Bochs 则没有 pci 设备，使用固定地址

// 支持硬件滚屏：设置 virt_height > height 可启用，之后通过 bga_set_offset()
// 改变显示起始行，无需 memcpy 即可滚屏。


#include "bga.h"
#include <pci.h>

#include <cpu/rw.h>
#include <arch_config.h>
#include <debug.h>
#include <arch_api.h>
#include <vmspace.h>

// BGA PCI 设备标识
#define BGA_PCI_VENDOR  0x1234
#define BGA_PCI_DEVICE  0x1111


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

// 扫描 bus 0 寻找 BGA 设备，读取 BAR0 返回 framebuffer 物理地址
// 未找到返回 0，调用方应回退到 BGA_FB_FALLBACK
INIT_TEXT uint32_t bga_get_address() {
    pci_dev_t *dev = pci_find(BGA_PCI_VENDOR, BGA_PCI_DEVICE);
    if (NULL == dev) {
        logk("not found bga in PCI\n");
        return BGA_FB_FALLBACK;
    }

    char is_io;
    char prefetchable;
    return pci_get_bar(dev, 0, &is_io, &prefetchable);
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
