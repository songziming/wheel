#ifndef MULTIBOOT1_H
#define MULTIBOOT1_H

// multiboot specification version 1
// https://www.gnu.org/software/grub/manual/multiboot/multiboot.html

#define MB1_HEADER_MAGIC                0x1badb002
#define MB1_BOOTLOADER_MAGIC            0x2badb002

#define MB1_HEADER_PAGE_ALIGN           0x00000001
#define MB1_HEADER_MEMORY_INFO          0x00000002
#define MB1_HEADER_VIDEO_MODE           0x00000004
#define MB1_HEADER_AOUT_KLUDGE          0x00010000

#define MB1_INFO_MEMORY                 0x00000001
#define MB1_INFO_BOOTDEV                0x00000002
#define MB1_INFO_CMDLINE                0x00000004
#define MB1_INFO_MODS                   0x00000008
#define MB1_INFO_AOUT_SYMS              0x00000010
#define MB1_INFO_ELF_SHDR               0X00000020
#define MB1_INFO_MEM_MAP                0x00000040
#define MB1_INFO_DRIVE_INFO             0x00000080
#define MB1_INFO_CONFIG_TABLE           0x00000100
#define MB1_INFO_BOOT_LOADER_NAME       0x00000200
#define MB1_INFO_APM_TABLE              0x00000400
#define MB1_INFO_VBE_INFO               0x00000800
#define MB1_INFO_FRAMEBUFFER_INFO       0x00001000

#ifdef C_FILE

#include <base.h>

// The symbol table for a.out
typedef struct mb1_aout_sym_tbl {
    uint32_t tabsize;
    uint32_t strsize;
    uint32_t addr;
    uint32_t reserved;
} PACKED mb1_aout_sym_tbl_t;

// The section header table for elf
typedef struct mb1_elf_sec_tbl {
    uint32_t num;
    uint32_t size;
    uint32_t addr;
    uint32_t shndx;
} PACKED mb1_elf_sec_tbl_t;

typedef struct mb1_info {
    uint32_t flags;
    uint32_t mem_lower;
    uint32_t mem_upper;
    uint32_t boot_device;
    uint32_t cmdline;
    uint32_t mods_count;
    uint32_t mods_addr;
    union {
        mb1_aout_sym_tbl_t aout;
        mb1_elf_sec_tbl_t  elf;
    };
    uint32_t mmap_length;
    uint32_t mmap_addr;
    uint32_t drives_length;
    uint32_t drives_addr;
    uint32_t config_table;
    uint32_t boot_loader_name;
    uint32_t apm_table;
    uint32_t vbe_ctrl_info;
    uint32_t vbe_mode_info;
    uint16_t vbe_mode;
    uint16_t vbe_interface_seg;
    uint16_t vbe_interface_off;
    uint16_t vbe_interface_len;
    uint64_t fb_addr;
    uint32_t fb_pitch;
    uint32_t fb_width;
    uint32_t fb_height;
    uint8_t  fb_bpp; // bits per pixel
    uint8_t  fb_type;
    uint8_t  fb_color_info[6];
} PACKED mb1_info_t;

enum {
    MB1_MEMORY_AVAILABLE = 1,
    // MB1_MEMORY_RESERVED  = 2,
    MB1_MEMORY_ACPI_RECLAIMABLE = 3,
    MB1_MEMORY_NV = 4, // preserved on hibernation
    MB1_MEMORY_BAD = 5,
};

typedef struct mb1_mmap_entry {
    uint32_t size;
    uint64_t addr;
    uint64_t len;
    uint32_t type;
} PACKED mb1_mmap_entry_t;

typedef struct mb1_mod_list {
    uint32_t mod_start;
    uint32_t mod_end;
    uint32_t cmdline; // module command line
    uint32_t padding; // must be zero
} PACKED mb1_mod_list_t;

#endif // C_FILE

#endif // MULTIBOOT1_H
