#include <dev/framebuf.h>
#include <wheel.h>
#include <str.h>


static CONST uint32_t g_rows = 0;
static CONST uint32_t g_cols = 0;
static CONST uint32_t g_size = 0;   // 一个像素多少字节
static CONST uint64_t g_step = 0;   // 一行多少节数，可能不是整数个像素

static CONST uint8_t *g_addr = NULL;    // framebuffer 映射的虚拟地址
static CONST uint8_t *g_back = NULL;    // 离屏缓冲区

static CONST uint32_t g_px_r; // 红色纯色
static CONST uint32_t g_px_g; // 绿色纯色
static CONST uint32_t g_px_b; // 蓝色纯色


INIT_TEXT void framebuf_init(fb_info_t *fb) {
    ASSERT(NULL == g_addr);
    ASSERT(NULL == g_back);

    g_rows = fb->rows;
    g_cols = fb->cols;
    g_size = fb->bpp >> 3;
    g_step = fb->pitch;

    g_addr = (uint8_t *)(DIRECT_MAP_ADDR + fb->addr);

    g_back = early_alloc_rw(g_rows * g_step);
    bset(g_back, 0, g_rows * g_step);

    g_px_r = ((1U << fb->r.mask_size) - 1) << fb->r.position;
    g_px_g = ((1U << fb->g.mask_size) - 1) << fb->g.position;
    g_px_b = ((1U << fb->b.mask_size) - 1) << fb->b.position;
}
