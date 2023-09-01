#include <dev/out_framebuf.h>
#include <debug.h>
#include <libk_string.h>


// 滚屏操作需要大量的内存复制
// 必须使用优化版本的 memcpy，否则速度很慢


extern font_data_t g_font_terminux_32x16;
extern font_data_t g_font_terminux_16x8;
extern font_data_t g_font_ubuntumono_16x8;

static uint8_t *g_backbuf;  // 离屏缓冲区
static uint8_t *g_addr;     // frame buffer 虚拟地址
static uint64_t g_step;     // 一行多少节数，可能不是整数个像素
static uint32_t g_px_size;  // 一个像素多少字节
static uint32_t g_px_rows;
static uint32_t g_px_cols;

static uint32_t g_px_r;
static uint32_t g_px_g;
static uint32_t g_px_b;

static font_data_t *g_font; // 目前使用的字体信息
static int g_em_rows;       // 当前行数
static int g_em_cols;       // 当前列数

static int g_caret_row;     // 光标所在行号
static int g_caret_col;     // 光标所在列号


INIT_TEXT void framebuf_init(mb2_tag_framebuffer_t *fb) {
    if ((1 != fb->type) || (32 != fb->bpp)) {
        dbg_print("framebuf only support 32-bit color mode!\n");
        return;
    }

    g_addr = (uint8_t *)(DIRECT_MAP_BASE + fb->addr);
    g_step = fb->pitch;
    g_px_size = fb->bpp >> 3;
    g_px_rows = fb->height;
    g_px_cols = fb->width;

    // 申请离屏缓冲区
    size_t backsize = g_px_rows * g_step;
    g_backbuf = early_alloc_rw(backsize);
    kmemset(g_backbuf, 0, backsize);

    g_px_r = ((1U << fb->r_mask_size) - 1) << fb->r_field_position;
    g_px_g = ((1U << fb->g_mask_size) - 1) << fb->g_field_position;
    g_px_b = ((1U << fb->b_mask_size) - 1) << fb->b_field_position;

    g_font = &g_font_terminux_16x8;
    g_em_rows = g_px_rows / g_font->height;
    g_em_cols = g_px_cols / g_font->width;

    g_caret_row = 0;
    g_caret_col = 0;
}

static void framebuf_putc_at(char ch, uint32_t fg, int r, int c) {
    ASSERT(r >= 0);
    ASSERT(r < g_em_rows);
    ASSERT(c >= 0);
    ASSERT(c < g_em_cols);

    const uint8_t *font_data = g_font->data + ch * g_font->char_size;
    int fb_offset = r * g_font->height * g_step + c * g_font->width * g_px_size;

    for (int y = 0; y < g_font->height; ++y) {
        uint32_t *fb_line = (uint32_t *)(g_addr    + fb_offset);
        uint32_t *fb_back = (uint32_t *)(g_backbuf + fb_offset);
        for (int x = 0; x < g_font->width; ++x) {
            uint8_t bit = 0x80 >> (x & 7);
            if (font_data[x >> 3] & bit) {
                fb_line[x] = fg;
                fb_back[x] = fg;
            } else {
                fb_line[x] = 0L;
                fb_back[x] = 0L;
            }
        }
        fb_offset += g_step;
        font_data += g_font->char_step;
    }
}

void framebuf_putc(char c) {
    // 更新光标位置
    switch (c) {
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
    default:
        framebuf_putc_at(c, g_px_r | g_px_g | g_px_b, g_caret_row, g_caret_col);
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
        uint64_t line_size = g_font->height * g_step;
        kmemcpy(g_backbuf, g_backbuf + line_size, (g_em_rows - 1) * line_size);
        kmemset(g_backbuf + (g_em_rows - 1) * line_size, 0, line_size);
        kmemcpy(g_addr, g_backbuf, g_px_rows * g_step);
        --g_caret_row;
    }
}

void framebuf_puts(const char *s, size_t n) {
    for (size_t i = 0; i < n; ++i) {
        framebuf_putc(s[i]);
    }
}
