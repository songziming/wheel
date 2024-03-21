// VESA BIOS Extension

#include <wheel.h>
#include <init/multiboot2.h>


typedef struct crtc_info_blk {
    //
} crtc_info_blk_t;

typedef struct vbe_info_block {
    char        sig[4]; // "VESA"
    uint16_t    version;
    uint32_t    oem_str;
    uint32_t    capabilities;
    uint32_t    video_modes;
    uint16_t    total_mem;
    uint16_t    oem_software_rev;
    uint32_t    oem_vendor_name;
    uint32_t    oem_product_name;
    uint32_t    oem_product_rev;
} PACKED vbe_info_block_t;


// TODO 不应直接接收 GRUB tag，应该像 framebuf 一样，自己定义一套结构体
//      multiboot 1 也可以返回 VBE 信息
void vbe_parse(mb2_tag_vbe_t *tag) {
    vbe_info_block_t *info = (vbe_info_block_t *)tag->vbe_control_info;

    klog("vbe sig %.4s, version %d\n", info->sig, info->version);
}
