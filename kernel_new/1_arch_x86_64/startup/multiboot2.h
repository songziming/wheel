#ifndef MULTIBOOT2_H
#define MULTIBOOT2_H

// multiboot specification version 2
// https://www.gnu.org/software/grub/manual/multiboot2/multiboot.html

#define MB2_HEADER_MAGIC        0xe85250d6 // the magic field should contain this
#define MB2_BOOTLOADER_MAGIC    0x36d76289 // this should be in %eax

#define MB2_MOD_ALIGN           0x00001000 // alignment of multiboot modules
#define MB2_INFO_ALIGN          0x00000008 // alignment of the multiboot info structure

#define MB2_TAG_ALIGN                  8
#define MB2_TAG_TYPE_END               0
#define MB2_TAG_TYPE_CMDLINE           1
#define MB2_TAG_TYPE_BOOT_LOADER_NAME  2
#define MB2_TAG_TYPE_MODULE            3
#define MB2_TAG_TYPE_BASIC_MEMINFO     4
#define MB2_TAG_TYPE_BOOTDEV           5
#define MB2_TAG_TYPE_MMAP              6
#define MB2_TAG_TYPE_VBE               7
#define MB2_TAG_TYPE_FRAMEBUFFER       8
#define MB2_TAG_TYPE_ELF_SECTIONS      9
#define MB2_TAG_TYPE_APM               10
#define MB2_TAG_TYPE_EFI32             11
#define MB2_TAG_TYPE_EFI64             12
#define MB2_TAG_TYPE_SMBIOS            13
#define MB2_TAG_TYPE_ACPI_OLD          14
#define MB2_TAG_TYPE_ACPI_NEW          15
#define MB2_TAG_TYPE_NETWORK           16
#define MB2_TAG_TYPE_EFI_MMAP          17
#define MB2_TAG_TYPE_EFI_BS            18
#define MB2_TAG_TYPE_EFI32_IH          19
#define MB2_TAG_TYPE_EFI64_IH          20
#define MB2_TAG_TYPE_LOAD_BASE_ADDR    21

#define MB2_HEADER_TAG_END                  0
#define MB2_HEADER_TAG_INFORMATION_REQUEST  1
#define MB2_HEADER_TAG_ADDRESS              2
#define MB2_HEADER_TAG_ENTRY_ADDRESS        3
#define MB2_HEADER_TAG_CONSOLE_FLAGS        4
#define MB2_HEADER_TAG_FRAMEBUFFER          5
#define MB2_HEADER_TAG_MODULE_ALIGN         6
#define MB2_HEADER_TAG_EFI_BS               7
#define MB2_HEADER_TAG_ENTRY_ADDRESS_EFI32  8
#define MB2_HEADER_TAG_ENTRY_ADDRESS_EFI64  9
#define MB2_HEADER_TAG_RELOCATABLE          10

#define MB2_ARCHITECTURE_I386   0
#define MB2_ARCHITECTURE_MIPS32 4
#define MB2_HEADER_TAG_OPTIONAL 1

#define MB2_LOAD_PREFERENCE_NONE 0
#define MB2_LOAD_PREFERENCE_LOW  1
#define MB2_LOAD_PREFERENCE_HIGH 2

#define MB2_CONSOLE_FLAGS_CONSOLE_REQUIRED   1
#define MB2_CONSOLE_FLAGS_EGA_TEXT_SUPPORTED 2

#ifdef C_FILE

#include <def.h>

typedef struct mb2_header {
    uint32_t magic;         // must be MB2_MAGIC
    uint32_t architecture;  // ISA
    uint32_t header_length; // total header length
    uint32_t checksum;      // the above fields plus this one must equal 0 mod 2^32
} mb2_header_t;

typedef struct mb2_header_tag {
    uint16_t type;
    uint16_t flags;
    uint32_t size;
} mb2_header_tag_t;

typedef struct mb2_header_tag_information_request {
    uint16_t type;
    uint16_t flags;
    uint32_t size;
    uint32_t requests[0];
} mb2_header_tag_information_request_t;

typedef struct mb2_header_tag_address {
    uint16_t type;
    uint16_t flags;
    uint32_t size;
    uint32_t header_addr;
    uint32_t load_addr;
    uint32_t load_end_addr;
    uint32_t bss_end_addr;
} mb2_header_tag_address_t;

typedef struct mb2_header_tag_entry_address {
    uint16_t type;
    uint16_t flags;
    uint32_t size;
    uint32_t entry_addr;
} mb2_header_tag_entry_address_t;

typedef struct mb2_header_tag_console_flags {
    uint16_t type;
    uint16_t flags;
    uint32_t size;
    uint32_t console_flags;
} mb2_header_tag_console_flags_t;

typedef struct mb2_header_tag_framebuffer {
    uint16_t type;
    uint16_t flags;
    uint32_t size;
    uint32_t width;
    uint32_t height;
    uint32_t depth;
} mb2_header_tag_framebuffer_t;

typedef struct mb2_header_tag_module_align {
    uint16_t type;
    uint16_t flags;
    uint32_t size;
} mb2_header_tag_module_align_t;

typedef struct mb2_header_tag_relocatable {
    uint16_t type;
    uint16_t flags;
    uint32_t size;
    uint32_t min_addr;
    uint32_t max_addr;
    uint32_t align;
    uint32_t preference;
} mb2_header_tag_relocatable_t;

typedef struct mb2_color {
    uint8_t red;
    uint8_t green;
    uint8_t blue;
} mb2_color_t;

enum {
    MB2_MEMORY_AVAILABLE        = 1,
    MB2_MEMORY_RESERVED         = 2,
    MB2_MEMORY_ACPI_RECLAIMABLE = 3,
    MB2_MEMORY_NVS              = 4,
    MB2_MEMORY_BADRAM           = 5,
};

typedef struct mb2_mmap_entry {
    uint64_t addr;
    uint64_t len;
    uint32_t type;
    uint32_t zero;
} mb2_mmap_entry_t;

typedef struct mb2_tag {
    uint32_t type;
    uint32_t size;
} mb2_tag_t;

typedef struct mb2_tag_string {
    mb2_tag_t tag;
    char     string[0];
} mb2_tag_string_t;

typedef struct mb2_tag_module {
    mb2_tag_t tag;
    uint32_t mod_start;
    uint32_t mod_end;
    char     cmdline[0];
} mb2_tag_module_t;

typedef struct mb2_tag_basic_meminfo {
    mb2_tag_t tag;
    uint32_t mem_lower;
    uint32_t mem_upper;
} mb2_tag_basic_meminfo_t;

typedef struct mb2_tag_bootdev {
    mb2_tag_t tag;
    uint32_t biosdev;
    uint32_t slice;
    uint32_t part;
} mb2_tag_bootdev_t;

typedef struct mb2_tag_mmap {
    mb2_tag_t tag;
    uint32_t entry_size;
    uint32_t entry_version;
    mb2_mmap_entry_t entries[0];
} mb2_tag_mmap_t;

typedef struct mb2_tag_vbe {
    mb2_tag_t tag;

    uint16_t vbe_mode;
    uint16_t vbe_interface_seg;
    uint16_t vbe_interface_off;
    uint16_t vbe_interface_len;

    uint8_t vbe_control_info[512];
    uint8_t vbe_mode_info[256];
} mb2_tag_vbe_t;

enum {
    MB2_FRAMEBUFFER_TYPE_INDEXED  = 0,
    MB2_FRAMEBUFFER_TYPE_RGB      = 1,
    MB2_FRAMEBUFFER_TYPE_EGA_TEXT = 2,
};

typedef struct mb2_tag_framebuffer {
    mb2_tag_t tag;

    uint64_t addr;
    uint32_t pitch;
    uint32_t width;
    uint32_t height;
    uint8_t  bpp;
    uint8_t  type;
    uint16_t reserved;

    union {
        struct {
            uint16_t palette_num_colors;
            mb2_color_t palette[0];
        };
        struct {
            uint8_t r_field_position;
            uint8_t r_mask_size;
            uint8_t g_field_position;
            uint8_t g_mask_size;
            uint8_t b_field_position;
            uint8_t b_mask_size;
        };
    };
} mb2_tag_framebuffer_t;

// multiboot 2 文档内容有错误，以 grub2 仓库中的 multiboot.h 为准
typedef struct mb2_tag_elf_sections {
    mb2_tag_t tag;
    uint32_t num;
    uint32_t entsize;
    uint32_t shndx;
    char     sections[0];
} mb2_tag_elf_sections_t;

typedef struct mb2_tag_apm {
    mb2_tag_t tag;
    uint16_t version;
    uint16_t cseg;
    uint32_t offset;
    uint16_t cseg_16;
    uint16_t dseg;
    uint16_t flags;
    uint16_t cseg_len;
    uint16_t cseg_16_len;
    uint16_t dseg_len;
} mb2_tag_apm_t;

typedef struct mb2_tag_efi32 {
    mb2_tag_t tag;
    uint32_t pointer;
} mb2_tag_efi32_t;

typedef struct mb2_tag_efi64 {
    mb2_tag_t tag;
    uint64_t pointer;
} mb2_tag_efi64_t;

typedef struct mb2_tag_smbios {
    mb2_tag_t tag;
    uint8_t  major;
    uint8_t  minor;
    uint8_t  reserved[6];
    uint8_t  tables[0];
} mb2_tag_smbios_t;

typedef struct mb2_tag_old_acpi {
    mb2_tag_t tag;
    uint8_t  rsdp[0];
} mb2_tag_old_acpi_t;

typedef struct mb2_tag_new_acpi {
    mb2_tag_t tag;
    uint8_t  rsdp[0];
} mb2_tag_new_acpi_t;

typedef struct mb2_tag_network {
    mb2_tag_t tag;
    uint8_t  dhcpack[0];
} mb2_tag_network_t;

typedef struct mb2_tag_efi_mmap {
    mb2_tag_t tag;
    uint32_t descr_size;
    uint32_t descr_vers;
    uint8_t  efi_mmap[0];
} mb2_tag_efi_mmap_t;

typedef struct mb2_tag_efi32_image_handle {
    mb2_tag_t tag;
    uint32_t pointer;
} mb2_tag_efi32_image_handle_t;

typedef struct mb2_tag_efi64_image_handle {
    mb2_tag_t tag;
    uint64_t pointer;
} mb2_tag_efi64_image_handle_t;

typedef struct mb2_tag_load_base_addr {
    mb2_tag_t tag;
    uint32_t load_base_addr;
} mb2_tag_load_base_addr_t;

#endif // C_FILE

#endif // MULTIBOOT2_H
