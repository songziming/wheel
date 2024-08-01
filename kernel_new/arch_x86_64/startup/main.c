#include <wheel.h>
#include <debug.h>
#include <symbols.h>
#include <mem_map.h>

#include "multiboot1.h"
#include "multiboot2.h"

#include <devices/serial.h>
#include <devices/console.h>
#include <devices/framebuf.h>

#include <devices/acpi.h>
#include <devices/acpi_madt.h>


static INIT_DATA size_t g_rsdp = 0;
static INIT_DATA int g_is_graphical = 0;


//------------------------------------------------------------------------------
// 解析物理内存布局
//------------------------------------------------------------------------------

static INIT_TEXT void mb1_parse_mmap(uint32_t mmap, uint32_t len) {
    int range_num = 0;
    for (uint32_t off = 0; off < len;) {
        mb1_mmap_entry_t *ent = (mb1_mmap_entry_t *)(size_t)(mmap + off);
        off += ent->size + sizeof(ent->size);
        ++range_num;
    }

    mem_map_reserve(range_num);

    for (uint32_t off = 0; off < len;) {
        mb1_mmap_entry_t *ent = (mb1_mmap_entry_t *)(size_t)(mmap +off);
        off += ent->size + sizeof(ent->size);

        mem_type_t type = (MB1_MEMORY_AVAILABLE == ent->type)
            ? MEM_AVAILABLE : MEM_RESERVED;

        mem_map_add(ent->addr, ent->addr + ent->len, type);
    }
}

static INIT_TEXT void mb2_parse_mmap(void *tag) {
    mb2_tag_mmap_t *mmap = (mb2_tag_mmap_t *)tag;
    uint32_t mmap_len = mmap->tag.size - sizeof(mb2_tag_mmap_t);
    int range_num = (int)(mmap_len / mmap->entry_size);

    mem_map_reserve(range_num);

    for (int i = 0; i < range_num; ++i) {
        mb2_mmap_entry_t *ent = &mmap->entries[i];

        mem_type_t type;
        switch (ent->type) {
        case MB2_MEMORY_AVAILABLE:        type = MEM_AVAILABLE;   break;
        case MB2_MEMORY_ACPI_RECLAIMABLE: type = MEM_RECLAIMABLE; break;
        default:                          type = MEM_RESERVED;    break;
        }

        mem_map_add(ent->addr, ent->addr + ent->len, type);
    }
}


//------------------------------------------------------------------------------
// 解析引导器信息
//------------------------------------------------------------------------------

static INIT_TEXT void mb1_init(uint32_t ebx UNUSED) {
    mb1_info_t *info = (mb1_info_t *)(size_t)ebx;

    if (MB1_INFO_MEM_MAP & info->flags) {
        mb1_parse_mmap(info->mmap_addr, info->mmap_length);
    }

    if (MB1_INFO_ELF_SHDR & info->flags) {
        // log("MB1 elf symbols\n");
        // parse_kernel_symtab(
        // (void *)(size_t)info->elf.addr, info->elf.size, info->elf.num);
        mb1_elf_sec_tbl_t *elf = &info->elf;
        parse_kernel_symtab((void *)(size_t)elf->addr, elf->size, elf->num);
    }

    if (MB1_INFO_FRAMEBUFFER_INFO & info->flags) {
        if (1 == info->fb_type && 32 == info->fb_bpp) {
            g_is_graphical = 1;
            framebuf_init(info->fb_height, info->fb_width, info->fb_pitch, info->fb_addr);

            uint32_t fg = 0;
            fg |= ((1U << info->r_mask_size) - 1) << info->r_field_position;
            fg |= ((1U << info->g_mask_size) - 1) << info->g_field_position;
            fg |= ((1U << info->b_mask_size) - 1) << info->b_field_position;
            framebuf_set_color(fg);
        }
    }
}

static INIT_TEXT void mb2_init(uint32_t ebx UNUSED) {
    size_t info = (size_t)ebx;
    uint32_t total_size = *(uint32_t *)info;

    uint32_t offset = 8;
    while (offset < total_size) {
        mb2_tag_t *tag = (mb2_tag_t *)(info + offset);
        offset += (tag->size + 7) & ~7;

        switch (tag->type) {
        // case MB2_TAG_TYPE_END:
        //     return;
        // case MB2_TAG_TYPE_CMDLINE:
        // case MB2_TAG_TYPE_BOOT_LOADER_NAME:
        //     continue;
        case MB2_TAG_TYPE_MMAP:
            mb2_parse_mmap(tag);
            break;
        case MB2_TAG_TYPE_ELF_SECTIONS: {
            mb2_tag_elf_sections_t *elf = (mb2_tag_elf_sections_t *)tag;
            parse_kernel_symtab(elf->sections, elf->entsize, elf->num);
            break;
        }
        case MB2_TAG_TYPE_FRAMEBUFFER: {
            mb2_tag_framebuffer_t *fb = (mb2_tag_framebuffer_t *)tag;
            if (1 == fb->type && 32 == fb->bpp) {
                g_is_graphical = 1;
                framebuf_init(fb->height, fb->width, fb->pitch, fb->addr);

                uint32_t fg = 0;
                fg |= ((1U << fb->r_mask_size) - 1) << fb->r_field_position;
                fg |= ((1U << fb->g_mask_size) - 1) << fb->g_field_position;
                fg |= ((1U << fb->b_mask_size) - 1) << fb->b_field_position;
                framebuf_set_color(fg);
            }
            break;
        }
        case MB2_TAG_TYPE_ACPI_OLD:
            g_rsdp = (size_t)((mb2_tag_old_acpi_t *)tag)->rsdp;
            break;
        case MB2_TAG_TYPE_ACPI_NEW:
            g_rsdp = (size_t)((mb2_tag_new_acpi_t *)tag)->rsdp;
            break;
        default:
            break;
        }
    }
}


//------------------------------------------------------------------------------
// 解析多核信息
//------------------------------------------------------------------------------

static CONST uint32_t *g_irq_to_gsi = NULL;
static CONST uint8_t  *g_gsi_to_irq = NULL;
static CONST uint16_t *g_gsi_flags  = NULL;

// 遍历 MADT 两次，首先统计 loapic、ioapic 个数，然后申请空间，复制信息
static INIT_TEXT void parse_madt(madt_t *madt) {
    uint64_t loapic_addr = madt->loapic_addr;

    // 这几个变量适合改为全局变量
    int loapic_num = 0;
    int ioapic_num = 0;
    uint32_t gsi_max = 0;
    uint32_t irq_max = 0;

    // 第一次遍历，统计数量
    for (size_t i = sizeof(madt_t); i < madt->header.length;) {
        acpi_subtbl_t *sub = (acpi_subtbl_t *)((size_t)madt + i);
        i += sub->length;

        switch (sub->type) {
        case MADT_TYPE_LOCAL_APIC_OVERRIDE:
            loapic_addr = ((madt_loapic_override_t *)sub)->address;
            break;
        case MADT_TYPE_LOCAL_APIC: {
            madt_loapic_t *lo = (madt_loapic_t *)sub;
            if ((1 & lo->loapic_flags) && (loapic_num < MAX_CPU_COUNT)) {
                ++loapic_num;
                // if (g_max_id < lo->id) {
                //     g_max_id = lo->id;
                // }
            }
            break;
        }
        case MADT_TYPE_LOCAL_X2APIC: {
            madt_lox2apic_t *lo = (madt_lox2apic_t *)sub;
            if ((1 & lo->loapic_flags) && (loapic_num < MAX_CPU_COUNT)) {
                ++loapic_num;
                // if (g_max_id < lo->id) {
                //     g_max_id = lo->id;
                // }
            }
            break;
        }
        case MADT_TYPE_IO_APIC:
            ++ioapic_num;
            break;
        case MADT_TYPE_INTERRUPT_OVERRIDE: {
            madt_int_override_t *override = (madt_int_override_t *)sub;
            if (override->source > irq_max) {
                irq_max = override->source;
            }
            if (override->gsi > gsi_max) {
                gsi_max = override->gsi;
            }
            break;
        }
        case MADT_TYPE_NMI_SOURCE: {
            madt_nmi_t *nmi = (madt_nmi_t *)sub;
            if (nmi->gsi > gsi_max) {
                gsi_max = nmi->gsi;
            }
            break;
        }
        default:
            break;
        }
    }

    g_irq_to_gsi = early_alloc_ro((irq_max + 1) * sizeof(uint32_t));
    g_gsi_to_irq = early_alloc_ro((gsi_max + 1) * sizeof(uint8_t));
    g_gsi_flags  = early_alloc_ro((gsi_max + 1) * sizeof(uint16_t));

    // 默认情况下，8259 IRQ 0~15 与 GSI 0~15 对应
    // 传统 ISA 中断为边沿触发
    for (uint8_t i = 0; i < irq_max; ++i) {
        g_irq_to_gsi[i] = i;
    }
    for (uint32_t i = 0; (i < gsi_max) && (i < 256); ++i) {
        g_gsi_to_irq[i] = i;
        g_gsi_flags[i] = TRIGMODE_EDGE;
    }

    // 第二次遍历，记录信息
    int loapic_idx = 0;
    int ioapic_idx = 0;
    for (size_t i = sizeof(madt_t); i < madt->header.length;) {
        acpi_subtbl_t *sub = (acpi_subtbl_t *)((size_t)madt + i);
        i += sub->length;

        switch (sub->type)  {
        case MADT_TYPE_LOCAL_APIC: {
            madt_loapic_t *lo = (madt_loapic_t *)sub;
            if (1 & lo->loapic_flags) {
                // g_loapics[loapic_idx].apic_id      = lo->id;
                // g_loapics[loapic_idx].processor_id = lo->processor_id;
                // g_loapics[loapic_idx].flags        = lo->loapic_flags;
                ++loapic_idx;
            }
            break;
        }
        default:
            break;
        }
    }

    log("loapic_addr=%d\n", loapic_addr);
}


//------------------------------------------------------------------------------
// 系统初始化入口点
//------------------------------------------------------------------------------

static void text_log(const char *s, size_t n) {
    serial_puts(s, n);
    console_puts(s, n);
}

static void gui_log(const char *s, size_t n) {
    serial_puts(s, n);
    framebuf_puts(s, n);
}

INIT_TEXT NORETURN void sys_init(uint32_t eax, uint32_t ebx) {
    serial_init();
    set_log_func(serial_puts);

    // 解析 multiboot 信息
    switch (eax) {
    case MB1_BOOTLOADER_MAGIC: mb1_init(ebx); break;
    case MB2_BOOTLOADER_MAGIC: mb2_init(ebx); break;
    default:
        log("fatal: unknown multibooot magic 0x%08x\n", eax);
        goto end;
    }

    if (g_is_graphical) {
        set_log_func(gui_log);
    } else {
        console_init();
        set_log_func(text_log);
    }

    log("welcome to wheel os\n");
    log("build time %s %s\n", __DATE__, __TIME__);

    if (0 == g_rsdp) {
        g_rsdp = acpi_probe_rsdp();
    }
    if (0 == g_rsdp) {
        log("fatal: ACPI::RSDP not found\n");
        goto end;
    }
    acpi_parse_rsdp(g_rsdp);

    madt_t *madt = (madt_t *)acpi_find_table("APIC");
    if (NULL == madt) {
        log("fatal: MADT not found!\n");
        goto end;
    }

    dump_symbols();

    acpi_show_tables();

end:
    while (1) {}
}
