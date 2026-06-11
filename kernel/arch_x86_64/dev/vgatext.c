#include "vgatext.h"
#include <arch_api.h>
#include <spinlock.h>
#include <kstring.h>
#include <console.h>


// VGA 寄存器映射端口
#define VGA_CRTC_ADDR 0x03d4
#define VGA_CRTC_DATA 0x03d5

// 默认宽高
#define ROWS 25
#define COLS 80

static CONST uint16_t *g_vram = NULL;   // LFB
static uint16_t g_vbuf[ROWS * COLS];    // 离屏缓冲区
static unsigned g_start_row;            // g_vram 首行在 g_vbuf 中的行号

static uint8_t  g_text_color;
static CONST display_ops_t ops; // 终端接口实现


static void set_caret(int x, int y) {
    uint16_t idx = y * COLS + x;
    out8(VGA_CRTC_ADDR, 0x0f); // caret location low
    out8(VGA_CRTC_DATA, idx & 0xff);
    out8(VGA_CRTC_ADDR, 0x0e); // caret location high
    out8(VGA_CRTC_DATA, (idx >> 8) & 0xff);
}

static void vgatext_draw_char(char ch, int x, int y) {
    uint16_t fill = (uint16_t)ch | ((uint16_t)g_text_color << 8);
    g_vbuf[y % ROWS * COLS + x] = fill;
    unsigned vram_row = y - g_start_row + ROWS;
    g_vram[vram_row % ROWS * COLS + x] = fill;
}

INIT_TEXT void vgatext_init() {
    g_text_color = 0x0f; // 黑底白字
    g_start_row = 0;

    // g_caret_row = 0;
    // g_caret_col = 0;

    // 两屏内容清空
    uint64_t *dst = (uint64_t*)g_vbuf;
    size_t len = ROWS * COLS * sizeof(uint16_t) / sizeof(uint64_t);
    uint64_t fill = (uint64_t)' ' | ((uint64_t)g_text_color << 8);
    fill |= fill << 16;
    fill |= fill << 32;
    for (size_t i = 0; i < len; ++i) {
        dst[i] = fill;
    }

    // 映射到 higher half，启动完成后低地址会取消映射
    g_vram = (uint16_t*)(IDENTITY_MAP_ADDR + 0xb8000);
    kmemcpy(g_vram, g_vbuf, ROWS*COLS * sizeof(uint16_t));

    ops.width = COLS;
    ops.height = ROWS;
    ops.draw_char = vgatext_draw_char;
    ops.draw_caret = set_caret;
    g_display = &ops;
}
