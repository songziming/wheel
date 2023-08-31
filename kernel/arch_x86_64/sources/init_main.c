// 初始化程序


#include <base.h>
#include <debug.h>

#include <arch_api.h>
#include <multiboot1.h>
#include <multiboot2.h>

#include <arch_mem.h>
#include <arch_smp.h>

#include <dev/acpi.h>
#include <dev/out_serial.h>
#include <dev/out_console.h>
#include <dev/out_framebuf.h>



static INIT_DATA acpi_rsdp_t *g_rsdp = NULL;
static INIT_DATA mb2_tag_framebuffer_t *g_mb2_fb = NULL;


// 启动阶段，multiboot info 和相关数据结构位于 lower-half
// 重要的数据使用 early_alloc 备份到 higher-half，这样启动完成后还能访问

static INIT_TEXT void mb1_init(uint32_t ebx) {
    mb1_info_t *info = (mb1_info_t *)(size_t)ebx;

    // 记录本机物理内存布局
    // 首先统计内存范围的数量，提前申请好空间，然后填充数据
    if (MB1_INFO_MEM_MAP & info->flags) {
        int map_len = 0;
        for (uint32_t off = 0; off < info->mmap_length;) {
            mb1_mmap_entry_t *ent = (mb1_mmap_entry_t *)(size_t)(info->mmap_addr + off);
            off += ent->size + sizeof(ent->size);
            ++map_len;
        }
        rammap_reserve(map_len);
        int map_idx = 0;
        for (uint32_t off = 0; off < info->mmap_length; ++map_idx) {
            mb1_mmap_entry_t *ent = (mb1_mmap_entry_t *)(size_t)(info->mmap_addr + off);
            off += ent->size + sizeof(ent->size);
            if (MB1_MEMORY_AVAILABLE == ent->type) {
                rammap_set(map_idx, RAM_AVAILABLE, ent->addr, ent->len);
            } else {
                rammap_set(map_idx, RAM_RESERVED, ent->addr, ent->len);
            }
        }
        ASSERT(map_idx == map_len);
    }

    if (MB1_INFO_ELF_SHDR & info->flags) {
        mb1_elf_sec_tbl_t *elf = &info->elf;
        symtab_init((void *)(size_t)elf->addr, elf->size, elf->num, elf->shndx);
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
            size_t len = (mmap->tag.size - sizeof(mb2_tag_mmap_t)) / mmap->entry_size;
            rammap_reserve(len);
            for (size_t i = 0; i < len; ++i) {
                mb2_mmap_entry_t *ent = &mmap->entries[i];
                ram_type_t type;
                switch (ent->type) {
                case MB2_MEMORY_AVAILABLE:        type = RAM_AVAILABLE;   break;
                case MB2_MEMORY_ACPI_RECLAIMABLE: type = RAM_RECLAIMABLE; break;
                default:                          type = RAM_RESERVED;    break;
                }
                rammap_set(i, type, ent->addr, ent->len);
            }
            break;
        }
        case MB2_TAG_TYPE_ELF_SECTIONS: {
            mb2_tag_elf_sections_t *elf = (mb2_tag_elf_sections_t *)tag;
            symtab_init(elf->sections, elf->entsize, elf->num, elf->shndx);
            break;
        }
        case MB2_TAG_TYPE_ACPI_OLD:
            g_rsdp = (acpi_rsdp_t *)((mb2_tag_old_acpi_t *)tag)->rsdp;
            break;
        case MB2_TAG_TYPE_ACPI_NEW:
            g_rsdp = (acpi_rsdp_t *)((mb2_tag_new_acpi_t *)tag)->rsdp;
            break;
        case MB2_TAG_TYPE_FRAMEBUFFER:
            g_mb2_fb = (mb2_tag_framebuffer_t *)tag;
            break;
        default:
            break;
        }
    }
}



// 图形终端输出回调
static void serial_framebuf_puts(const char *s, size_t n) {
    serial_puts(s, n);
    framebuf_puts(s, n);
}

// 字符终端输出回调
static void serial_console_puts(const char *s, size_t n) {
    serial_puts(s, n);
    console_puts(s, n);
}

INIT_TEXT NORETURN void sys_init(uint32_t eax, uint32_t ebx) {
    serial_init();
    g_dbg_print_func = serial_puts;

    switch (eax) {
    case MB1_BOOTLOADER_MAGIC: mb1_init(ebx); break;
    case MB2_BOOTLOADER_MAGIC: mb2_init(ebx); break;
    default:
        dbg_print("error: unknown multibooot magic %x\n", eax);
        goto end;
    }

    // 配置图形化终端或字符终端
    if (g_mb2_fb) {
        framebuf_init(g_mb2_fb);
        g_dbg_print_func = serial_framebuf_puts;
    } else {
        console_init();
        g_dbg_print_func = serial_console_puts;
    }

#ifdef DEBUG
    rammap_show();
    symtab_show();
#endif

    // 寻找并解析 ACPI 表
    if (NULL == g_rsdp) {
        g_rsdp = acpi_probe_rsdp();
    }
    if (NULL == g_rsdp) {
        dbg_print("error: RSDP not found!\n");
        goto end;
    }
    acpi_parse_rsdp(g_rsdp);

#ifdef DEBUG
    acpi_show_tables();
#endif

    // 解析 MADT
    madt_t *madt = (madt_t *)acpi_get_table("APIC");
    if (NULL == madt) {
        dbg_print("error: MADT not found!\n");
        goto end;
    }
    parse_madt(madt);

end:
    cpu_halt();
    while (1) {}
}

INIT_TEXT NORETURN void sys_init_ap(uint32_t ebx) {
    (void)ebx;

    cpu_halt();
    while (1) {}
}
