// 初始化程序


#include <base.h>
#include <debug.h>

#include <arch_api.h>
#include <multiboot1.h>
#include <multiboot2.h>

#include <arch_mem.h>

#include <dev/out_serial.h>



// 启动阶段，multiboot info 位于 lower-half

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
        ram_map_reserve(map_len);
        int map_idx = 0;
        for (uint32_t off = 0; off < info->mmap_length; ++map_idx) {
            mb1_mmap_entry_t *ent = (mb1_mmap_entry_t *)(size_t)(info->mmap_addr + off);
            off += ent->size + sizeof(ent->size);
            if (MB1_MEMORY_AVAILABLE == ent->type) {
                ram_map_set(map_idx, RAM_AVAILABLE, ent->addr, ent->len);
            } else {
                ram_map_set(map_idx, RAM_RESERVED, ent->addr, ent->len);
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
            ram_map_reserve(len);
            for (size_t i = 0; i < len; ++i) {
                mb2_mmap_entry_t *ent = &mmap->entries[i];
                ram_type_t type;
                switch (ent->type) {
                case MB2_MEMORY_AVAILABLE:        type = RAM_AVAILABLE;   break;
                case MB2_MEMORY_ACPI_RECLAIMABLE: type = RAM_RECLAIMABLE; break;
                default:                          type = RAM_RESERVED;    break;
                }
                ram_map_set(i, type, ent->addr, ent->len);
            }
            break;
        }
        case MB2_TAG_TYPE_ELF_SECTIONS: {
            mb2_tag_elf_sections_t *elf = (mb2_tag_elf_sections_t *)tag;
            symtab_init(elf->sections, elf->entsize, elf->num, elf->shndx);
            break;
        }
        default:
            break;
        }
    }
}

INIT_TEXT NORETURN void sys_init(uint32_t eax, uint32_t ebx) {
    serial_init();
    g_dbg_print_func = serial_puts;

    switch (eax) {
    case MB1_BOOTLOADER_MAGIC: mb1_init(ebx); break;
    case MB2_BOOTLOADER_MAGIC: mb2_init(ebx); break;
    default:
        dbg_print("error: unknown multibooot magic %x\n", eax);
        cpu_halt();
    }

#if DEBUG
    dbg_print("ram ranges:\n");
    ram_map_show();
#endif

    cpu_halt();
    while (1) {}
}

INIT_TEXT NORETURN void sys_init_ap(uint32_t ebx) {
    (void)ebx;

    cpu_halt();
    while (1) {}
}
