#include <startup/multiboot2.h>
#include <cpu/rw.h>
#include <library/debug.h>
#include <arch_impl.h>



typedef struct vbe_info_block {
    char        sig[4]; // "VESA"
    uint16_t    version; // BCD 编码，0x300 表示 v3.0，0x102 表示 v1.2
    uint16_t    oem_str[2];
    uint32_t    capabilities;
    uint16_t    video_modes[2];
    uint16_t    total_mem;

    // 以下字段在 VBE 2.0 加入
    uint16_t    oem_software_rev;
    uint16_t    oem_vendor_name[2];
    uint16_t    oem_product_name[2];
    uint16_t    oem_product_rev[2];
} PACKED vbe_info_block_t;



static void *far_pointer(uint16_t ptr[2]) {
    return (void*)(((size_t)ptr[1] << 4) + ptr[0] + DIRECT_MAP_ADDR);
}


// TODO 不应直接接收 GRUB tag，应该像 framebuf 一样，自己定义一套结构体
//      multiboot 1 也可以返回 VBE 信息
void vbe_parse(mb2_tag_vbe_t *tag) {
    vbe_info_block_t *info = (vbe_info_block_t*)tag->vbe_control_info;

    log("current mode %x, interface at %x:%x, len %x\n",
        tag->vbe_mode, tag->vbe_interface_seg, tag->vbe_interface_off,
        tag->vbe_interface_len);

    log("vbe sig %.4s, version %x\n", info->sig, info->version);
    log("oem %s\n", (char*)far_pointer(info->oem_str));

    if (info->version >= 0x200) {
        log("vendor name  %s\n", (char*)far_pointer(info->oem_vendor_name));
        log("product name %s\n", (char*)far_pointer(info->oem_product_name));
        log("product rev  %s\n", (char*)far_pointer(info->oem_product_rev));
    }

    if (info->capabilities & 1) {
        log("- DAC width switchable to 8-bits per color\n");
    } else {
        log("- DAC fixed 6-bits per color\n");
    }
    if (info->capabilities & 2) {
        log("- VGA incompatible!\n");
    }

    // 计算 framebuffer 内存总大小（不是所有 video mode 都能使用这部分完整的显存）
    size_t fbsize = (size_t)info->total_mem << 16;
    log("- frame buffer size 0x%lx\n", fbsize);

    // 获取支持的显示模式编号（数字可能重复）
    log("supported modes:");
    uint16_t *modes = far_pointer(info->video_modes);
    for (int i = 0; 0xffff != modes[i]; ++i) {
        if (0 == modes[i]) {
            continue;
        }
        log(" %x", modes[i]);
    }
    log("\n");

    // 拿到的每个显示模式，还要单独调用 VBE func 1，获取该模式的详细信息
}


//------------------------------------------------------------------------------
// bochs VBE 寄存器
//------------------------------------------------------------------------------

#define VBE_PORT_INDEX  0x01ce
#define VBE_PORT_DATA   0x01cf


#define VBE_INDEX_ID               0x0
#define VBE_INDEX_XRES             0x1
#define VBE_INDEX_YRES             0x2
#define VBE_INDEX_BPP              0x3
#define VBE_INDEX_ENABLE           0x4
#define VBE_INDEX_BANK             0x5
#define VBE_INDEX_VIRT_WIDTH       0x6
#define VBE_INDEX_VIRT_HEIGHT      0x7
#define VBE_INDEX_X_OFFSET         0x8
#define VBE_INDEX_Y_OFFSET         0x9
#define VBE_INDEX_VIDEO_MEMORY_64K 0xa
#define VBE_INDEX_DDC              0xb



#define VBE_DISPI_TOTAL_VIDEO_MEMORY_MB  16
#define VBE_DISPI_4BPP_PLANE_SHIFT       22

#define VBE_DISPI_BANK_ADDRESS           0xA0000

#define VBE_DISPI_MAX_XRES               2560
#define VBE_DISPI_MAX_YRES               1600
#define VBE_DISPI_MAX_BPP                32

#define VBE_DISPI_ID0                    0xB0C0
#define VBE_DISPI_ID1                    0xB0C1
#define VBE_DISPI_ID2                    0xB0C2
#define VBE_DISPI_ID3                    0xB0C3
#define VBE_DISPI_ID4                    0xB0C4
#define VBE_DISPI_ID5                    0xB0C5

#define VBE_DISPI_BPP_4                  0x04
#define VBE_DISPI_BPP_8                  0x08
#define VBE_DISPI_BPP_15                 0x0F
#define VBE_DISPI_BPP_16                 0x10
#define VBE_DISPI_BPP_24                 0x18
#define VBE_DISPI_BPP_32                 0x20

#define VBE_DISPI_DISABLED               0x00
#define VBE_DISPI_ENABLED                0x01
#define VBE_DISPI_GETCAPS                0x02
#define VBE_DISPI_BANK_GRANULARITY_32K   0x10
#define VBE_DISPI_8BIT_DAC               0x20
#define VBE_DISPI_LFB_ENABLED            0x40
#define VBE_DISPI_NOCLEARMEM             0x80

#define VBE_DISPI_BANK_WR                0x4000
#define VBE_DISPI_BANK_RD                0x8000
#define VBE_DISPI_BANK_RW                0xc000

#define VBE_DISPI_LFB_PHYSICAL_ADDRESS   0xE0000000

#define VBE_DISPI_TOTAL_VIDEO_MEMORY_KB  (VBE_DISPI_TOTAL_VIDEO_MEMORY_MB * 1024)
#define VBE_DISPI_TOTAL_VIDEO_MEMORY_BYTES (VBE_DISPI_TOTAL_VIDEO_MEMORY_KB * 1024)

static uint16_t vbe_read(uint16_t reg) {
    out16(VBE_PORT_INDEX, reg);
    return in16(VBE_PORT_DATA);
}

// static void vbe_write(uint16_t reg, uint16_t val) {
//     out16(VBE_PORT_INDEX, reg);
//     out16(VBE_PORT_DATA, val);
// }

INIT_TEXT void bga_init() {
    if (VBE_DISPI_ID5 != vbe_read(VBE_INDEX_ID)) {
        return;
    }

    log("found bochs vga!\n");
}
