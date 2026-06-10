#include "framebuf.h"
#include "bga.h"

#include <console.h>
#include <arch_config.h>
#include <arch_api.h>
#include <cpu/rw.h>
#include <kstring.h>
#include <spinlock.h>
#include <early_alloc.h>
#include <vmspace.h>

#include <debug.h>


// 点阵字体
extern const fbfont_t g_font_terminux_32x16;
extern const fbfont_t g_font_terminux_16x8;
extern const fbfont_t g_font_ubuntumono_16x8;
static const fbfont_t * const g_font = &g_font_terminux_16x8;

// 以像素为单位：
static CONST uint32_t g_rows  = 0;  // 屏幕高度（单位：像素）
static CONST uint32_t g_cols  = 0;  // 屏幕宽度（单位：像素）
static CONST uint64_t g_pitch = 0;  // 一行多少字节，可能不是整数个像素

static INIT_DATA size_t g_size = 0;     // LFB 大小（用于映射 WC）
static CONST uint8_t *g_addr = NULL;    // LFB 映射地址（最好映射为 WC）
static CONST uint8_t *g_back = NULL;    // 离屏缓冲区（用于无 bga 滚屏）

// 如果有 BGA，该字段表示 disp-y 偏移量，取值 [0,ops.height)
// 如果没有 BGA，则表示 back-buffer 中哪一行对应屏幕开头（单位：字符）
static int g_disp_top = 0;

static uint32_t g_framebuf_color = 0xffffffff;   // 像素颜色
static CONST display_ops_t ops; // 终端接口实现

//------------------------------------------------------------------------------
// 无 BGA，有 backbuffer
//------------------------------------------------------------------------------

// LFB 无法硬件滚屏，无法设置显示区域
// 滚屏的时候只能改变 back->LFB 的相对位置，然后全屏 copy

// 同时绘制到 LFB 和 back-buffer
// 也可能是不可见字符
static void nobga_draw_char_at(char ch, int c, int r) {
    int back_r = (r + g_disp_top) % ops.height;
    int fb_pos = r * g_font->rows * g_pitch + c * g_font->cols * sizeof(uint32_t);
    int back_pos = back_r * g_font->rows * g_pitch + c * g_font->cols * sizeof(uint32_t);

    const uint8_t *font_data = g_font->data + ch * g_font->size;
    for (int y = 0; y < g_font->rows; ++y) {
        uint32_t *fb_line = (uint32_t*)(g_addr + fb_pos);
        uint32_t *fb_back = (uint32_t*)(g_back + back_pos);
        for (int x = 0; x < g_font->cols; ++x) {
            uint8_t bit = 0x80 >> (x & 7);
            if (font_data[x >> 3] & bit) {
                fb_line[x] = g_framebuf_color;
                fb_back[x] = g_framebuf_color;
            } else {
                fb_line[x] = 0L;
                fb_back[x] = 0L;
            }
        }
        back_pos += g_pitch;
        fb_pos += g_pitch;
        font_data += g_font->pitch;
    }
}

// 类似绘制字符，只是字符内容全部为前景
// TODO 光标是否也要在 back-buffer 里面标记出来？后面打印不可见字符时可以将其抹掉
static void nobga_draw_caret_at(int c, int r) {
    int back_r = (r + g_disp_top) % ops.height;
    int fb_pos = r * g_font->rows * g_pitch + c * g_font->cols * sizeof(uint32_t);
    int back_pos = back_r * g_font->rows * g_pitch + c * g_font->cols * sizeof(uint32_t);

    for (int y = 0; y < g_font->rows; ++y) {
        uint32_t *fb_line = (uint32_t*)(g_addr + fb_pos);
        uint32_t *fb_back = (uint32_t*)(g_back + back_pos);
        for (int x = 0; x < g_font->cols; ++x) {
            fb_line[x] = g_framebuf_color;
            fb_back[x] = g_framebuf_color;
        }
        back_pos += g_pitch;
        fb_pos += g_pitch;
    }
}

// 滚动若干行
static void nobga_scroll(int nrows) {
    uint64_t line_size = g_font->rows * g_pitch;

    // 把显示出来的新行清空
    for (int i = 0; i < nrows; ++i) {
        g_disp_top = (g_disp_top + 1) % ops.height;
        int new_bottom = (g_disp_top + ops.height - 1) % ops.height;
        kmemset(g_back + new_bottom * line_size, 0, line_size);
    }

    // 将整屏画面从 back-buffer 拷贝到 LFB
    int tail = ops.height - g_disp_top;
    kmemcpy(g_addr, g_back + g_disp_top * line_size, tail * line_size);
    kmemcpy(g_addr + tail * line_size, g_back, g_disp_top * line_size);
}

//------------------------------------------------------------------------------
// 有 BGA，LFB 包括两屏，使用硬件滚屏，不需要 back-buffer
//------------------------------------------------------------------------------

static void bga_draw_char_at(char ch, int c, int r) {
    int fb_r = r + g_disp_top;

    // 绘制 LFB 后半部分，也要同时绘制开头，也就是 ops.height 行之前
    for (; fb_r >= 0; fb_r -= ops.height) {
        int fb_pos = fb_r * g_font->rows * g_pitch + c * g_font->cols * sizeof(uint32_t);
        const uint8_t *font_data = g_font->data + ch * g_font->size;
        for (int y = 0; y < g_font->rows; ++y) {
            uint32_t *fb_line = (uint32_t*)(g_addr + fb_pos);
            for (int x = 0; x < g_font->cols; ++x) {
                uint8_t bit = 0x80 >> (x & 7);
                if (font_data[x >> 3] & bit) {
                    fb_line[x] = g_framebuf_color;
                } else {
                    fb_line[x] = 0L;
                }
            }
            fb_pos += g_pitch;
            font_data += g_font->pitch;
        }
    }
}

static void bga_draw_caret_at(int c, int r) {
    int fb_r = r + g_disp_top;

    // 绘制 LFB 后半部分，也要同时绘制开头，也就是 ops.height 行之前
    for (; fb_r >= 0; fb_r -= ops.height) {
        int fb_pos = fb_r * g_font->rows * g_pitch + c * g_font->cols * sizeof(uint32_t);
        for (int y = 0; y < g_font->rows; ++y) {
            uint32_t *fb_line = (uint32_t*)(g_addr + fb_pos);
            for (int x = 0; x < g_font->cols; ++x) {
                fb_line[x] = g_framebuf_color;
            }
            fb_pos += g_pitch;
        }
    }
}

static void bga_scroll(int nrows) {
    g_disp_top = (g_disp_top + nrows) % ops.height;

    // 新露出来的行清空（一定位于 LFB 后半部分）
    int newlines = g_disp_top + ops.height - nrows;
    uint64_t line_size = g_font->rows * g_pitch;
    kmemset(g_addr + newlines * line_size, 0, line_size * nrows);

    // LFB 前半部分也要清空
    if (g_disp_top <= nrows) {
        kmemset(g_addr, 0, line_size * g_disp_top);
    } else {
        kmemset(g_addr + (g_disp_top - nrows) * line_size, 0, line_size * nrows);
    }

    bga_set_offset(0, g_disp_top * g_font->rows);
}

//------------------------------------------------------------------------------
// 公开 API
//------------------------------------------------------------------------------

// static CONST void (*g_draw_char)(char ch, uint32_t fg, int r, int c);
// static CONST void (*g_draw_caret)(uint32_t fg, int r, int c);
// static CONST void (*g_scroll)(int nrows);

INIT_TEXT void framebuf_init(uint32_t rows, uint32_t cols, uint32_t pitch, uint32_t addr) {
    g_size = rows * pitch;

    int has_bga = 0;
    if (bga_check()) {
        // 虚拟环境下，调用 BGA 设置虚拟缓冲区，利用硬件滚屏功能
        // 我们可以自己选择宽高 1280x960，32-bit 色深
        // virt_height = 2*960，分配两个屏幕高度的虚拟 framebuffer
        if (bga_config(1280, 960, 32, 1280, 2*960)) {
            rows = 960;
            cols = 1280;
            pitch = 1280 * 4;    // 32-bit
            addr = bga_get_address();
            g_size = BGA_FB_SIZE;
            has_bga = 1;
        } else {
            bga_disable();
        }
    }

    g_rows = rows;
    g_cols = cols;
    g_pitch = pitch;

    // TODO 映射到 Write-Combined region
    g_addr = (uint8_t*)(DIRECT_MAP_ADDR + addr);
    g_disp_top = 0;

    // 如果没有 BGA，则需要另一个离屏缓冲区
    if (has_bga) {
        ops.draw_char  = bga_draw_char_at;
        ops.draw_caret = bga_draw_caret_at;
        ops.scroll     = bga_scroll;
    } else {
        g_back = early_alloc_rw(g_rows * g_pitch);
        kmemset(g_back, 0, g_rows * g_pitch);
        ops.draw_char  = nobga_draw_char_at;
        ops.draw_caret = nobga_draw_caret_at;
        ops.scroll     = nobga_scroll;
    }

    ops.height = g_rows / g_font->rows;
    ops.width  = g_cols / g_font->cols;
    g_display = &ops;
}

// 将 LFB 重新映射为 write-combined，提升写入速度
INIT_TEXT void framebuf_remap_wc() {
    size_t va  = (size_t)g_addr;
    size_t vend = va + g_size;
    size_t pa = va - DIRECT_MAP_ADDR;
    vend += PAGE_SIZE - 1;
    vend &= ~(PAGE_SIZE - 1);
    logk("remapping 0x%zx~0x%zx as WC\n", va, vend);

    mmu_unmap(g_kernel_vm.table, va, vend);
    mmu_map(g_kernel_vm.table, va, vend, pa, MMU_WRITE|MMU_WC);
}

void framebuf_setfg(uint32_t fg) {
    g_framebuf_color = fg;
}
