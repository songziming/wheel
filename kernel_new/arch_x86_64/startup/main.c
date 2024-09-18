#include <wheel.h>
#include <library/debug.h>
#include <library/symbols.h>
#include <memory/pmlayout.h>
#include <memory/early_alloc.h>

#include "multiboot1.h"
#include "multiboot2.h"

#include <devices/serial.h>
#include <devices/console.h>
#include <devices/framebuf.h>

#include <devices/acpi.h>
#include <devices/acpi_madt.h>

#include <generic/cpufeatures.h>
#include <generic/smp.h>
#include <generic/gdt_idt_tss.h>

#include <memory/mem_init.h>


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

    pmranges_alloc(range_num);

    for (uint32_t off = 0; off < len;) {
        mb1_mmap_entry_t *ent = (mb1_mmap_entry_t *)(size_t)(mmap +off);
        off += ent->size + sizeof(ent->size);

        pmtype_t type = (MB1_MEMORY_AVAILABLE == ent->type)
            ? PM_AVAILABLE : PM_RESERVED;

        pmrange_add(ent->addr, ent->addr + ent->len, type);
    }
}

static INIT_TEXT void mb2_parse_mmap(void *tag) {
    mb2_tag_mmap_t *mmap = (mb2_tag_mmap_t *)tag;
    uint32_t mmap_len = mmap->tag.size - sizeof(mb2_tag_mmap_t);
    int range_num = (int)(mmap_len / mmap->entry_size);

    pmranges_alloc(range_num);

    for (int i = 0; i < range_num; ++i) {
        mb2_mmap_entry_t *ent = &mmap->entries[i];

        pmtype_t type;
        switch (ent->type) {
        case MB2_MEMORY_AVAILABLE:        type = PM_AVAILABLE;   break;
        case MB2_MEMORY_ACPI_RECLAIMABLE: type = PM_RECLAIMABLE; break;
        default:                          type = PM_RESERVED;    break;
        }

        pmrange_add(ent->addr, ent->addr + ent->len, type);
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
        parse_kernel_symtab((void *)(size_t)elf->addr, elf->size, elf->num, elf->shndx);
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
            parse_kernel_symtab(elf->sections, elf->entsize, elf->num, elf->shndx);
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

    cpu_features_detect();
    cpu_features_enable();

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

    madt_t *madt = (madt_t *)acpi_table_find("APIC");
    if (NULL == madt) {
        log("fatal: MADT not found!\n");
        goto end;
    }
    parse_madt(madt);

    // TODO 检查 Acpi::DMAR，判断是否需要 interrupt remapping
    // TODO 检查 Acpi::SRAT，获取 numa 信息（个人电脑一般不需要）

    // 关键数据已经备份，可以放开 early-alloc 长度限制
    early_rw_unlock();

    // TODO 检查 Acpi::MCFG，分析 PCIe 信息
    // TODO 遍历 PCI 总线，识别并注册 PCI 外设

    // 切换到正式的 GDT，加载 IDT
    gdt_init();
    gdt_load();
    idt_init();
    idt_load();

    // 内存管理初始化
    // 禁用临时内存管理，之后使用 page_alloc
    mem_init();

    // TODO 划分内存布局，启用物理页面管理
    // gsbase_init(0);





    pmlayout_show(); // 打印物理内存布局
    acpi_tables_show(); // 打印 acpi 表
    cpu_features_show(); // 打印 cpuinfo

end:
    while (1) {}
}
