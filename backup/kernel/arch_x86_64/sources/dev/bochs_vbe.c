// BGA - Bochs Graphics Adaptor
// 参考 Bochs 源码中的文件 bochs/iodev/display/vga.h

#include <wheel.h>
#include <cpu/rw.h>


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

    klog("found bochs vga!\n");
}
