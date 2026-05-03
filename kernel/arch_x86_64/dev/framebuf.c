#include "framebuf.h"
#include "bga.h"

#include <arch_config.h>
#include <kstring.h>
#include <spin.h>
#include <early_alloc.h>

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

// 以字符为单位：
static CONST unsigned g_em_rows;    // 当前字体下的行数
static CONST unsigned g_em_cols;    // 当前字体下的列数
static unsigned g_caret_row;        // 光标所在字符行号（逻辑行号，0 = 屏幕顶）
static unsigned g_caret_col;        // 光标所在字符列号

// linear framebuffer 映射的虚拟地址
// TODO framebuf 的页表映射应该使用 WC (Write-Combining)
static CONST uint8_t *g_addr = NULL;

// 环形离屏缓冲区（永远只存一屏，不需要滚屏）
static CONST uint8_t *g_back = NULL;

// 如果有 BGA，该字段表示 disp-y 偏移量，取值 [0,g_em_rows)
// 如果没有 BGA，则表示 back-buffer 中哪一行对应屏幕开头（单位：字符）
static unsigned g_disp_top = 0;

CONST uint32_t g_framebuf_color = 0xffffffff;   // 像素颜色
static spin_t g_framebuf_lock = SPIN_INIT;


//------------------------------------------------------------------------------
// 无 BGA，有 backbuffer
//------------------------------------------------------------------------------

// LFB 无法硬件滚屏，无法设置显示区域
// 滚屏的时候只能改变 back->LFB 的相对位置，然后全屏 copy

// 同时绘制到 LFB 和 back-buffer
// 也可能是不可见字符
static void nobga_draw_char_at(char ch, uint32_t fg, int r, int c) {
    int back_r = (r + g_disp_top) & g_em_rows;
    int fb_pos = r * g_font->rows * g_pitch + c * g_font->cols * sizeof(uint32_t);
    int back_pos = back_r * g_font->rows * g_pitch + c * g_font->cols * sizeof(uint32_t);

    const uint8_t *font_data = g_font->data + ch * g_font->size;
    for (unsigned y = 0; y < g_font->rows; ++y) {
        uint32_t *fb_line = (uint32_t*)(g_addr + fb_pos);
        uint32_t *fb_back = (uint32_t*)(g_back + back_pos);
        for (unsigned x = 0; x < g_font->cols; ++x) {
            uint8_t bit = 0x80 >> (x & 7);
            if (font_data[x >> 3] & bit) {
                fb_line[x] = fg;
                fb_back[x] = fg;
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
static void nobga_draw_caret_at(uint32_t fg, int r, int c) {
    int back_r = (r + g_disp_top) & g_em_rows;
    int fb_pos = r * g_font->rows * g_pitch + c * g_font->cols * sizeof(uint32_t);
    int back_pos = back_r * g_font->rows * g_pitch + c * g_font->cols * sizeof(uint32_t);

    for (unsigned y = 0; y < g_font->rows; ++y) {
        uint32_t *fb_line = (uint32_t*)(g_addr + fb_pos);
        uint32_t *fb_back = (uint32_t*)(g_back + back_pos);
        for (unsigned x = 0; x < g_font->cols; ++x) {
            fb_line[x] = fg;
            fb_back[x] = fg;
        }
        back_pos += g_pitch;
        fb_pos += g_pitch;
    }
}

// 滚动若干行
static void nobga_scroll(unsigned nrows) {
    uint64_t line_size = g_font->rows * g_pitch;

    // 把显示出来的新行清空
    for (unsigned i = 0; i < nrows; ++i) {
        g_disp_top = (g_disp_top + 1) % g_em_rows;
        unsigned new_bottom = (g_disp_top + g_em_rows - 1) % g_em_rows;
        kmemset(g_back + new_bottom * line_size, 0, line_size);
    }

    // 将整屏画面从 back-buffer 拷贝到 LFB
    unsigned tail = g_em_rows - g_disp_top;
    kmemcpy(g_addr, g_back + g_disp_top * line_size, tail * line_size);
    kmemcpy(g_addr + tail * line_size, g_back, g_disp_top * line_size);
}

//------------------------------------------------------------------------------
// 有 BGA，LFB 包括两屏，使用硬件滚屏，不需要 back-buffer
//------------------------------------------------------------------------------

static void bga_draw_char_at(char ch, uint32_t fg, int r, int c) {
    int fb_r = r + g_disp_top;

    // 绘制 LFB 后半部分，也要同时绘制开头，也就是 g_em_rows 行之前
    for (; fb_r >= 0; fb_r -= g_em_rows) {
        int fb_pos = fb_r * g_font->rows * g_pitch + c * g_font->cols * sizeof(uint32_t);
        const uint8_t *font_data = g_font->data + ch * g_font->size;
        for (unsigned y = 0; y < g_font->rows; ++y) {
            uint32_t *fb_line = (uint32_t*)(g_addr + fb_pos);
            for (unsigned x = 0; x < g_font->cols; ++x) {
                uint8_t bit = 0x80 >> (x & 7);
                if (font_data[x >> 3] & bit) {
                    fb_line[x] = fg;
                } else {
                    fb_line[x] = 0L;
                }
            }
            fb_pos += g_pitch;
            font_data += g_font->pitch;
        }
    }
}

static void bga_draw_caret_at(uint32_t fg, int r, int c) {
    int fb_r = r + g_disp_top;

    // 绘制 LFB 后半部分，也要同时绘制开头，也就是 g_em_rows 行之前
    for (; fb_r >= 0; fb_r -= g_em_rows) {
        int fb_pos = fb_r * g_font->rows * g_pitch + c * g_font->cols * sizeof(uint32_t);
        for (unsigned y = 0; y < g_font->rows; ++y) {
            uint32_t *fb_line = (uint32_t*)(g_addr + fb_pos);
            for (unsigned x = 0; x < g_font->cols; ++x) {
                fb_line[x] = fg;
            }
            fb_pos += g_pitch;
        }
    }
}

static void bga_scroll(unsigned nrows) {
    g_disp_top = (g_disp_top + nrows) % g_em_rows;

    // 新露出来的行清空（一定位于 LFB 后半部分）
    unsigned newlines = g_disp_top + g_em_rows - nrows;
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

static CONST void (*g_draw_char)(char ch, uint32_t fg, int r, int c);
static CONST void (*g_draw_caret)(uint32_t fg, int r, int c);
static CONST void (*g_scroll)(unsigned nrows);

INIT_TEXT void framebuf_init(uint32_t rows, uint32_t cols, uint32_t pitch, uint32_t addr) {
    // spin_init(&g_framebuf_lock);

    // 虚拟环境下，调用 BGA 设置虚拟缓冲区，利用硬件滚屏功能
    int has_bga = 0;
    if (bga_check()) {
        // 虚拟机，有 BGA 支持，我们可以自己选择宽高 1280x960
        // virt_height = 2*960，分配两个屏幕高度的虚拟 framebuffer
        if (bga_config(1280, 960, 32, 1280, 2*960)) {
            // logk("BGA FB addr 0x%x->0x%x\n", addr, BGA_FB_ADDR);
            rows = 960;
            cols = 1280;
            pitch = 1280 * 4;    // 32-bit
            addr = bga_get_address();
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

    // 如果没有 BGA，则需要另一个离屏缓冲区
    if (has_bga) {
        g_draw_char = bga_draw_char_at;
        g_draw_caret = bga_draw_caret_at;
        g_scroll = bga_scroll;
    } else {
        g_back = early_alloc_rw(g_rows * g_pitch);
        kmemset(g_back, 0, g_rows * g_pitch);
        g_draw_char = nobga_draw_char_at;
        g_draw_caret = nobga_draw_caret_at;
        g_scroll = nobga_scroll;
    }

    g_em_rows = g_rows / g_font->rows;
    g_em_cols = g_cols / g_font->cols;
    g_caret_row = 0;
    g_caret_col = 0;
    g_disp_top = 0;

    g_draw_caret(g_framebuf_color, g_caret_row, g_caret_col);
}

// 将 LFB 重新映射为 write-combined，提升写入速度
INIT_TEXT void framebuf_remap_wc() {
    // TODO
}

void framebuf_setfg(uint32_t fg) {
    g_framebuf_color = fg;
}

static void framebuf_putc(char ch) {
    unsigned r = g_caret_row;
    unsigned c = g_caret_col;

    // 更新光标位置
    // 遇到非打印字符则换成空格，以清除光标
    switch (ch) {
    case '\t':
        g_caret_col += 8;
        g_caret_col &= ~7;
        ch = ' ';
        break;
    case '\n':
        ++g_caret_row;
        // fallthrough
    case '\r':
        g_caret_col = 0;
        ch = ' ';
        break;
    default:
        ++g_caret_col;
        break;
    }

    // 超过屏幕宽度则换行
    if (g_caret_col >= g_em_cols) {
        g_caret_col -= g_em_cols;
        ++g_caret_row;
    }

    // 超过屏幕高度，需要滚屏
    if (g_caret_row >= g_em_rows) {
        g_scroll(g_caret_row - g_em_rows + 1);
        --g_caret_row;
        --r;
    }

    g_draw_char(ch, g_framebuf_color, r, c);
}

void framebuf_puts(const char *s, size_t n) {
    int key = irq_spin_take(&g_framebuf_lock);
    for (size_t i = 0; i < n; ++i) {
        framebuf_putc(s[i]);
    }
    g_draw_caret(g_framebuf_color, g_caret_row, g_caret_col);
    irq_spin_give(&g_framebuf_lock, key);
}
