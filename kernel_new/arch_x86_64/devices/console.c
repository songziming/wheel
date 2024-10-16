#include "console.h"
#include <arch_impl.h>
#include <cpu/rw.h>
#include <memory/early_alloc.h>
#include <library/debug.h>
#include <library/string.h>
#include <library/spin.h>


// VGA 寄存器
#define VGA_CRTC_ADDR 0x03d4
#define VGA_CRTC_DATA 0x03d5

// 默认宽高
#define ROWS 25
#define COLS 80

static spin_t g_console_lock;

// 显存读取速度很慢，因此内存中预留离屏缓冲区
static CONST uint16_t *g_vbuf = NULL;
static CONST uint16_t *g_vram = NULL;

static uint8_t  g_text_color;
static unsigned g_caret_row;    // 光标所在行（g_vbuf）
static unsigned g_caret_col;    // 光标所在列
static unsigned g_start_row;    // g_vram 首行在 g_vbuf 中的行号


INIT_TEXT void console_init() {
    ASSERT(NULL == g_vram);
    ASSERT(NULL == g_vbuf);

    spin_init(&g_console_lock);

    g_text_color = 0x0f; // 黑底白字
    g_caret_row = 0;
    g_caret_col = 0;
    g_start_row = 0;

    // 两屏内容清空
    g_vbuf = early_alloc_rw(ROWS * COLS * sizeof(uint16_t));
    uint64_t *dst = (uint64_t*)g_vbuf;
    uint64_t fill = (uint64_t)' ' | ((uint64_t)g_text_color << 8);
    fill |= fill << 16;
    fill |= fill << 32;
    for (int i = 0; i < ROWS * COLS / 4; ++i) {
        dst[i] = fill;
    }

    // 映射到 higher half，启动完成后低地址会取消映射
    g_vram = (uint16_t*)(DIRECT_MAP_ADDR + 0xb8000);
    memcpy(g_vram, g_vbuf, ROWS*COLS * sizeof(uint16_t));
}

static void set_caret(uint16_t idx) {
    out8(VGA_CRTC_ADDR, 0x0f);              // caret location low
    out8(VGA_CRTC_DATA, idx & 0xff);
    out8(VGA_CRTC_ADDR, 0x0e);              // caret location high
    out8(VGA_CRTC_DATA, (idx >> 8) & 0xff);
}

// 显示一个字符，并更新 state
static void console_putc(char ch) {
    // 更新光标位置
    switch (ch) {
    case '\t':
        g_caret_col += 8;
        g_caret_col &= ~7;
        break;
    case '\n':
        ++g_caret_row;
        // fallthrough
    case '\r':
        g_caret_col = 0;
        break;
    default: {
        uint16_t fill = (uint16_t)ch | ((uint16_t)g_text_color << 8);
        g_vbuf[g_caret_row % ROWS * COLS + g_caret_col] = fill;
        unsigned vram_row = g_caret_row - g_start_row + ROWS;
        g_vram[vram_row % ROWS * COLS + g_caret_col] = fill;
        ++g_caret_col;
        break;
    }
    }

    // 超过屏幕宽度则换行
    if (g_caret_col >= COLS) {
        g_caret_col -= COLS;
        ++g_caret_row;
    }

    // 超过屏幕高度，需要滚屏
    // 将新行清空，重新复制离屏缓冲区到显存
    if (g_caret_row - g_start_row >= ROWS) {
        uint16_t *line = &g_vbuf[g_caret_row % ROWS * COLS];
        uint16_t fill = (uint16_t)' ' | ((uint16_t)g_text_color << 8);
        for (int i = 0; i < COLS; ++i) {
            line[i] = fill;
        }

        g_start_row = g_caret_row - ROWS + 1;
        for (int r = 0; r < ROWS; ++r) {
            int offset = (g_start_row + r) % ROWS * COLS;
            memcpy(&g_vram[r * COLS], &g_vbuf[offset], COLS * sizeof(uint16_t));
        }
    }
}

void console_puts(const char *s, size_t n) {
    int key = irq_spin_take(&g_console_lock);
    for (size_t i = 0; i < n; ++i) {
        console_putc(s[i]);
    }
    set_caret((g_caret_row - g_start_row) * COLS + g_caret_col);
    irq_spin_give(&g_console_lock, key);
}
