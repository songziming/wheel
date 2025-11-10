// VESA BIOS Extension，有三种使用方式：
//  - 实模式 int 0x10
//  - VBE 2.0 保护模式接口
//  - VBE 3.0 保护模式接口
// 不过，大部份资料都不建议使用保护模式接口，只能解析 GRUB 在实模式获取到的信息

#include <wheel.h>
#include <init/multiboot2.h>


// 结构体里的指针是 segment:offset 格式

typedef struct crtc_info_blk {
    //
} crtc_info_blk_t;

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



// VBE 3.0+ 定义的保护模式接口
// 需要在 BIOS image 开头 32K 范围内搜索这个结构体
typedef struct pminfo {
    char        sig[4]; // "PMID"
    uint16_t    entry_off;
    uint16_t    init_off;
    uint16_t    data_sel;
} pminfo_t;



static void *far_pointer(uint16_t ptr[2]) {
    return (void *)(((size_t)ptr[1] << 4) + ptr[0] + DIRECT_MAP_ADDR);
}

// TODO 不应直接接收 GRUB tag，应该像 framebuf 一样，自己定义一套结构体
//      multiboot 1 也可以返回 VBE 信息
void vbe_parse(mb2_tag_vbe_t *tag) {
    vbe_info_block_t *info = (vbe_info_block_t *)tag->vbe_control_info;

    klog("current mode %x, interface at %x:%x, len %x\n",
        tag->vbe_mode, tag->vbe_interface_seg, tag->vbe_interface_off,
        tag->vbe_interface_len);

    klog("vbe sig %.4s, version %x\n", info->sig, info->version);
    klog("oem %s\n", (char *)far_pointer(info->oem_str));

    if (info->version >= 0x200) {
        klog("vendor name  %s\n", (char *)far_pointer(info->oem_vendor_name));
        klog("product name %s\n", (char *)far_pointer(info->oem_product_name));
        klog("product rev  %s\n", (char *)far_pointer(info->oem_product_rev));
    }

    if (info->capabilities & 1) {
        klog("- DAC width switchable to 8-bits per color\n");
    } else {
        klog("- DAC fixed 6-bits per color\n");
    }
    if (info->capabilities & 2) {
        klog("- VGA incompatible!\n");
    }

    // 计算 framebuffer 内存总大小（不是所有 video mode 都能使用这部分完整的显存）
    size_t fbsize = (size_t)info->total_mem << 16;
    klog("- frame buffer size 0x%lx\n", fbsize);

    // 获取支持的显示模式编号（数字可能重复）
    klog("supported modes:");
    uint16_t *modes = far_pointer(info->video_modes);
    for (int i = 0; 0xffff != modes[i]; ++i) {
        if (0 == modes[i]) {
            continue;
        }
        klog(" %x", modes[i]);
    }
    klog("\n");

    // 拿到的每个显示模式，还要单独调用 VBE func 1，获取该模式的详细信息
}
