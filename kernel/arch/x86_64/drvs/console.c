#include <wheel.h>

#define VGA_CRTC_ADDR   0x03d4
#define VGA_CRTC_DATA   0x03d5

#define ROW_COUNT       25
#define COL_COUNT       80

#define IDX(row, col) ((((row) + ROW_COUNT) % ROW_COUNT) * COL_COUNT + (col))

// there's only one global console
// multiple virtual console is not supported
static u16 * video_ram;
static u16   video_buf[ROW_COUNT*COL_COUNT];
static int   caret_row;
static int   caret_col;
static int   start_row;
static u8    text_attr;

static void console_set_caret(u16 idx) {
    out8(VGA_CRTC_ADDR, 0x0f);              // caret location low
    out8(VGA_CRTC_DATA, idx & 0xff);
    out8(VGA_CRTC_ADDR, 0x0e);              // caret location high
    out8(VGA_CRTC_DATA, (idx >> 8) & 0xff);
}

static void console_put_char(char c) {
    u16 pixel = (u16) c | ((u16) text_attr << 8);

    // move caret accordingly
    switch (c) {
    case '\t':
        caret_col +=  8;
        caret_col &= ~7;
        break;
    case '\n':
        caret_row +=  1;
        caret_col  =  0;
        break;
    case '\r':
        caret_col  =  0;
        break;
    default:
        video_buf[IDX(caret_row,             caret_col)] = pixel;
        video_ram[IDX(caret_row - start_row, caret_col)] = pixel;
        caret_col +=  1;
        break;
    }

    // handle line wrap
    while (caret_col >= COL_COUNT) {
        caret_col -= COL_COUNT;
        caret_row += 1;
    }

    // handle scroll
    if (caret_row - start_row >= ROW_COUNT) {
        u64 fill = (u64) ' ' | ((u64) text_attr << 8);
        fill |= fill << 16;
        fill |= fill << 32;
        while (caret_row - start_row >= ROW_COUNT) {
            u64 * line = (u64 *) &video_buf[IDX(start_row, 0)];
            start_row += 1;
            for (int j = 0; j < COL_COUNT / 4; ++j) {
                line[j] = fill;
            }
        }
        for (int i = 0; i < ROW_COUNT; ++i) {
            memcpy(&video_ram[IDX(i, 0)],
                   &video_buf[IDX(start_row + i, 0)],
                   COL_COUNT * sizeof(u16));
        }
    }
}

usize console_write(const char * buf, usize len) {
    for (usize i = 0; i < len; ++i) {
        console_put_char(buf[i]);
    }
    console_set_caret(IDX(caret_row - start_row, caret_col));
    return len;
}

__INIT void console_dev_init() {
    video_ram = (u16 *) phys_to_virt(0xb8000);
    memset(video_buf, 0, sizeof(ROW_COUNT*COL_COUNT*sizeof(u16)));
    caret_row = 0;
    caret_col = 0;
    start_row = 0;
    text_attr = 0x0f;
}
