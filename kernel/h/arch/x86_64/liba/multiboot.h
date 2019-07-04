#ifndef ARCH_X86_64_LIBA_MULTIBOOT_H
#define ARCH_X86_64_LIBA_MULTIBOOT_H

#include <base.h>

// The symbol table for a.out.
typedef struct mb_aout_sym_tbl {
    u32 tabsize;
    u32 strsize;
    u32 addr;
    u32 reserved;
} __PACKED mb_aout_sym_tbl_t;

// The section header table for ELF.
typedef struct mb_elf_sec_tbl {
    u32 num;
    u32 size;
    u32 addr;
    u32 shndx;
} __PACKED mb_elf_sec_tbl_t;

typedef struct mb_info {
    u32 flags;
    u32 mem_lower;
    u32 mem_upper;
    u32 boot_device;
    u32 cmdline;
    u32 mods_count;
    u32 mods_addr;
    union {
        mb_aout_sym_tbl_t aout;
        mb_elf_sec_tbl_t  elf;
    };
    u32 mmap_length;
    u32 mmap_addr;
    u32 drives_length;
    u32 drives_addr;
    u32 config_table;
    u32 boot_loader_name;
    u32 apm_table;
    u32 vbe_ctrl_info;
    u32 vbe_mode_info;
    u16 vbe_mode;
    u16 vbe_interface_seg;
    u16 vbe_interface_off;
    u16 vbe_interface_len;
    u64 fb_addr;
    u32 fb_pitch;
    u32 fb_width;
    u32 fb_height;
    u8  fb_bpp;             // bits per pixel
    u8  fb_type;
    u8  fb_color_info[6];
} __PACKED mb_info_t;

typedef struct mb_mmap_item {
    u32 size;
    u64 addr;
    u64 len;
#define MB_MEMORY_AVAILABLE  1
#define MB_MEMORY_RESERVED   2
    u32 type;
} __PACKED mb_mmap_item_t;

typedef struct mb_mod_list {
    u32 mod_start;
    u32 mod_end;
    u32 cmdline;            // module command line
    u32 padding;            // must be zero
} __PACKED mb_mod_list_t;

#endif // ARCH_X86_64_LIBA_MULTIBOOT_H
