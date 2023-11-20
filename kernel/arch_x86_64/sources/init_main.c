#include <wheel.h>

#include <multiboot1.h>
#include <multiboot2.h>

#include <arch_mem.h>

#include <dev/acpi.h>
#include <dev/serial.h>
#include <dev/framebuf.h>



static INIT_DATA acpi_rsdp_t *g_rsdp = NULL;

typedef struct fb_info {
    size_t   addr;
    unsigned rows;
    unsigned cols;
    unsigned bpp;   // 一个像素多少比特（32）
    unsigned pitch; // 一行多少字节，可能不是整数个像素
    struct {
        uint8_t mask_size;
        uint8_t position;
    } r, g, b;
} fb_info_t;

static INIT_DATA fb_info_t g_fb;


static INIT_TEXT void mb1_init(uint32_t ebx) {
    mb1_info_t *info = (mb1_info_t *)(size_t)ebx;

    if (MB1_INFO_MEM_MAP & info->flags) {
        g_rammap_len = 0;
        for (uint32_t off = 0; off < info->mmap_length;) {
            mb1_mmap_entry_t *ent = (mb1_mmap_entry_t *)(size_t)(info->mmap_addr + off);
            off += ent->size + sizeof(ent->size);
            ++g_rammap_len;
        }

        g_rammap = early_alloc_ro(g_rammap_len * sizeof(ram_range_t));

        for (uint32_t off = 0, i = 0; off < info->mmap_length; ++i) {
            mb1_mmap_entry_t *ent = (mb1_mmap_entry_t *)(size_t)(info->mmap_addr + off);
            off += ent->size + sizeof(ent->size);
            if (MB1_MEMORY_AVAILABLE == ent->type) {
                g_rammap[i].type = RAM_AVAILABLE;
            } else {
                g_rammap[i].type = RAM_RESERVED;
            }
            g_rammap[i].addr = ent->addr;
            g_rammap[i].end  = ent->addr + ent->len;
        }
    }

    if (MB1_INFO_ELF_SHDR & info->flags) {
        mb1_elf_sec_tbl_t *elf = &info->elf;
        symtab_init((void *)(size_t)elf->addr, elf->size, elf->num);
    }

    if (MB1_INFO_FRAMEBUFFER_INFO & info->flags) {
        //
    }
}


static INIT_TEXT void mb2_init(uint32_t ebx) {
    size_t info = (size_t)ebx;
    uint32_t total_size = *(uint32_t *)info;

    uint32_t offset = 8;
    while (offset < total_size) {
        mb2_tag_t *tag = (mb2_tag_t *)(info + offset);
        offset += (tag->size + 7) & ~7;

        switch (tag->type) {
        case MB2_TAG_TYPE_MMAP: {
            mb2_tag_mmap_t *mmap = (mb2_tag_mmap_t *)tag;
            g_rammap_len = (mmap->tag.size - sizeof(mb2_tag_mmap_t)) / mmap->entry_size;
            g_rammap = early_alloc_ro(g_rammap_len * sizeof(ram_range_t));
            for (size_t i = 0; i < g_rammap_len; ++i) {
                mb2_mmap_entry_t *ent = &mmap->entries[i];
                switch (ent->type) {
                case MB2_MEMORY_AVAILABLE:        g_rammap[i].type = RAM_AVAILABLE;   break;
                case MB2_MEMORY_ACPI_RECLAIMABLE: g_rammap[i].type = RAM_RECLAIMABLE; break;
                default:                          g_rammap[i].type = RAM_RESERVED;    break;
                }
                g_rammap[i].addr = ent->addr;
                g_rammap[i].end  = ent->addr + ent->len;
            }
            break;
        }
        case MB2_TAG_TYPE_ELF_SECTIONS: {
            mb2_tag_elf_sections_t *elf = (mb2_tag_elf_sections_t *)tag;
            symtab_init(elf->sections, elf->entsize, elf->num);
            break;
        }
        case MB2_TAG_TYPE_ACPI_OLD:
            g_rsdp = (acpi_rsdp_t *)((mb2_tag_old_acpi_t *)tag)->rsdp;
            break;
        case MB2_TAG_TYPE_ACPI_NEW:
            g_rsdp = (acpi_rsdp_t *)((mb2_tag_new_acpi_t *)tag)->rsdp;
            break;
        case MB2_TAG_TYPE_FRAMEBUFFER: {
            mb2_tag_framebuffer_t *fb = (mb2_tag_framebuffer_t *)tag;
            if (1 != fb->type) {
                klog("warning: framebuf only support color mode!\n");
                break;
            }

            g_fb.addr = fb->addr;
            g_fb.rows = fb->height;
            g_fb.cols = fb->width;
            g_fb.pitch = fb->pitch;
            g_fb.bpp  = fb->bpp;

            g_fb.r.position = fb->r_field_position;
            g_fb.r.mask_size = fb->r_mask_size;
            g_fb.g.position = fb->g_field_position;
            g_fb.g.mask_size = fb->g_mask_size;
            g_fb.b.position = fb->b_field_position;
            g_fb.b.mask_size = fb->b_mask_size;
            break;
        }
        default:
            break;
        }
    }
}


// BSP 初始化函数
INIT_TEXT void sys_init(uint32_t eax, uint32_t ebx) {
    serial_init();
    set_log_func(serial_puts);

    switch (eax) {
    case MB1_BOOTLOADER_MAGIC: mb1_init(ebx); break;
    case MB2_BOOTLOADER_MAGIC: mb2_init(ebx); break;
    default:
        klog("fatal: unknown multibooot magic %x\n", eax);
        goto end;
    }
#ifdef DEBUG
    rammap_show();
#endif

    if (NULL == g_rsdp) {
        g_rsdp = acpi_find_rsdp();
    }
    if (NULL == g_rsdp) {
        klog("fatal: RSDP not found!\n");
        goto end;
    }

    acpi_parse_rsdp(g_rsdp);
#ifdef DEBUG
    acpi_show_tables();
#endif

    // TODO 将重要数据备份，放开 early_alloc_rw 长度限制，再初始化图形终端

end:
    emu_exit(0);
    while (1) {}
}
