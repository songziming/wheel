#include <wheel.h>

#include "multiboot1.h"
#include "multiboot2.h"

#include <dev/serial.h>
#include <dev/console.h>
#include <dev/framebuf.h>

#include <debug.h>
#include <kstring.h>


static INIT_DATA uint32_t g_fgcolor;
static INIT_DATA size_t   g_rsdp;


static void mb1_init(uint32_t ebx) {
    mb1_info_t *info = (mb1_info_t*)(size_t)ebx;

    // if (MB1_INFO_MEM_MAP & info->flags) {
    //     mb1_parse_mmap(info->mmap_addr, info->mmap_length);
    // }

    if (MB1_INFO_FRAMEBUFFER_INFO & info->flags) {
        if (1 == info->fb_type && 32 == info->fb_bpp) {
            g_fgcolor  = ((1U << info->r_size) - 1) << info->r_shift;
            g_fgcolor |= ((1U << info->g_size) - 1) << info->g_shift;
            g_fgcolor |= ((1U << info->b_size) - 1) << info->b_shift;
            framebuf_init(info->fb_height, info->fb_width, info->fb_pitch, info->fb_addr);
        }
    }
}

static void mb2_init(uint32_t ebx) {
    size_t info = (size_t)ebx;
    uint32_t size = *(uint32_t*)info;

    for (uint32_t off = 8; off < size;) {
        mb2_tag_t *tag = (mb2_tag_t*)(info + off);
        off += (tag->size + 7) & ~7;

        switch (tag->type) {
        case MB2_TAG_TYPE_END:
            return;
        // case MB2_TAG_TYPE_MMAP:
        //     mb2_parse_mmap(tag);
        //     break;

        case MB2_TAG_TYPE_FRAMEBUFFER: {
            mb2_tag_framebuffer_t *fb = (mb2_tag_framebuffer_t*)tag;
            if (1 == fb->type && 32 == fb->bpp) {
                g_fgcolor  = ((1U << fb->r_size) - 1) << fb->r_shift;
                g_fgcolor |= ((1U << fb->g_size) - 1) << fb->g_shift;
                g_fgcolor |= ((1U << fb->b_size) - 1) << fb->b_shift;
                framebuf_init(fb->height, fb->width, fb->pitch, fb->addr);
            }
            break;
        }
        case MB2_TAG_TYPE_ACPI_OLD:
            g_rsdp = (size_t)((mb2_tag_old_acpi_t*)tag)->rsdp;
            break;
        case MB2_TAG_TYPE_ACPI_NEW:
            g_rsdp = (size_t)((mb2_tag_new_acpi_t*)tag)->rsdp;
            break;
        default:
            break;
        }
    }
}


static INIT_TEXT void text_log(const char *s, size_t n) {
    serial_puts(s, n);
    console_puts(s, n);
}

static INIT_TEXT void gui_log(const char *s, size_t n) {
    serial_puts(s, n);
    framebuf_puts(s, n);
}

void sys_init(uint32_t eax, uint32_t ebx) {
    serial_init();
    g_log_func = serial_puts;

    g_fgcolor = 0;
    g_rsdp = 0;
    switch (eax) {
    case MB1_BOOTLOADER_MAGIC: mb1_init(ebx); break;
    case MB2_BOOTLOADER_MAGIC: mb2_init(ebx); break;
    }

    if (g_fgcolor) {
        g_log_func = gui_log;
    } else {
        console_init();
        g_log_func = text_log;
    }

    logk("welcome to wheel operating system!\n");

    while (1) {}
}
