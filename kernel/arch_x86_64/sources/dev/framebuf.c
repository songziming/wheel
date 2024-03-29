// 图形模式的字符终端

// TODO 支持 escape sequence

#include <dev/framebuf.h>
#include <wheel.h>
#include <spin.h>



static CONST uint32_t g_rows = 0;
static CONST uint32_t g_cols = 0;
static CONST uint32_t g_size = 0;   // 一个像素多少字节
static CONST uint64_t g_step = 0;   // 一行多少节数，可能不是整数个像素

static CONST uint8_t *g_addr = NULL;    // framebuffer 映射的虚拟地址
static CONST uint8_t *g_back = NULL;    // 离屏缓冲区

static CONST uint32_t g_px_r;       // 红色纯色
static CONST uint32_t g_px_g;       // 绿色纯色
static CONST uint32_t g_px_b;       // 蓝色纯色
static CONST uint32_t g_px_color;   // 像素颜色

extern font_data_t g_font_terminux_32x16;
extern font_data_t g_font_terminux_16x8;
extern font_data_t g_font_ubuntumono_16x8;

static font_data_t *g_font = NULL;  // 当前使用的字体
static int g_em_rows;               // 当前字体下的行数
static int g_em_cols;               // 当前字体下的列数
static int g_caret_row;             // 光标所在行号
static int g_caret_col;             // 光标所在列号

static spin_t g_framebuf_spin = SPIN_INIT;


static void framebuf_putc_at(char ch, uint32_t fg, int r, int c) {
    ASSERT(NULL != g_font);
    ASSERT(r >= 0);
    ASSERT(r < g_em_rows);
    ASSERT(c >= 0);
    ASSERT(c < g_em_cols);

    const uint8_t *font_data = g_font->data + ch * g_font->char_size;
    int fb_offset = r * g_font->height * g_step + c * g_font->width * g_size;

    for (int y = 0; y < g_font->height; ++y) {
        uint32_t *fb_line = (uint32_t *)(g_addr + fb_offset);
        uint32_t *fb_back = (uint32_t *)(g_back + fb_offset);
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

static void framebuf_draw_caret(int fg, int r, int c) {
    int fb_offset = r * g_font->height * g_step + c * g_font->width * g_size;

    for (int y = 0; y < g_font->height; ++y) {
        uint32_t *fb_line = (uint32_t *)(g_addr + fb_offset);
        uint32_t *fb_back = (uint32_t *)(g_back + fb_offset);
        for (int x = 0; x < g_font->width; ++x) {
            fb_line[x] = fg;
            fb_back[x] = fg;
        }
        fb_offset += g_step;
    }
}

INIT_TEXT void framebuf_init(const fb_info_t *fb) {
    ASSERT(NULL == g_addr);
    ASSERT(NULL == g_back);
    ASSERT(NULL == g_font);

    g_rows = fb->rows;
    g_cols = fb->cols;
    g_size = fb->bpp >> 3;
    g_step = fb->pitch;

    g_addr = (uint8_t *)(DIRECT_MAP_ADDR + fb->addr);

    g_back = early_alloc_rw(g_rows * g_step);
    memset(g_back, 0, g_rows * g_step);

    g_px_r = ((1U << fb->r.mask_size) - 1) << fb->r.position;
    g_px_g = ((1U << fb->g.mask_size) - 1) << fb->g.position;
    g_px_b = ((1U << fb->b.mask_size) - 1) << fb->b.position;
    g_px_color = g_px_r | g_px_g | g_px_b;

    g_font = &g_font_terminux_16x8;
    g_em_rows = g_rows / g_font->height;
    g_em_cols = g_cols / g_font->width;
    g_caret_row = 0;
    g_caret_col = 0;

    framebuf_draw_caret(g_px_color, g_caret_row, g_caret_col);
}

void framebuf_putc(char ch) {
    int r = g_caret_row;
    int c = g_caret_col;

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
        uint64_t line_size = g_font->height * g_step;
        memcpy(g_back, g_back + line_size, (g_em_rows - 1) * line_size);
        memset(g_back + (g_em_rows - 1) * line_size, 0, line_size);
        memcpy(g_addr, g_back, g_rows * g_step);
        --g_caret_row;
        --r;
    }

    framebuf_putc_at(ch, g_px_color, r, c);
}

void framebuf_puts(const char *s, size_t n) {
    int key = irq_spin_take(&g_framebuf_spin);
    for (size_t i = 0; i < n; ++i) {
        framebuf_putc(s[i]);
    }
    framebuf_draw_caret(g_px_color, g_caret_row, g_caret_col);
    irq_spin_give(&g_framebuf_spin, key);
}

// TODO 提供 ioctl 函数，支持修改分辨率、设置光标位置、颜色等
