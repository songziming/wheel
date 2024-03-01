// FAT 文件系统，包括 FAT12/FAT16/FAT32，以及 exFAT、VFAT

#include <fat.h>


// 整块 partition 分为四部分：
//  1. 引导扇区
//  2. FAT 表
//  3. 根目录区
//  4. 数据区





//------------------------------------------------------------------------------
// 超级块数据结构
//------------------------------------------------------------------------------

// bios parameter block, 25-byte
// 位于引导扇区开头
typedef struct bpb {
    uint16_t byts_per_sec;
    uint8_t  sec_per_clus;
    uint16_t rsvd_sec_cnt;
    uint8_t  num_fats;
    uint16_t root_ent_cnt;
    uint16_t tot_sec_16;
    uint8_t  media;
    uint16_t fat_sz_16;
    uint16_t sec_per_trk;
    uint16_t num_heads;
    uint32_t hidd_sec;
    uint32_t tot_sec_32;
} PACKED bpb_t;

// extended boot record
typedef struct ebr {
    uint8_t  drv_num;
    uint8_t  reserved;
    uint8_t  boot_sig;
    uint32_t vol_id;
    uint8_t  vol_label[11];
    uint8_t  fs_type[8];
} PACKED ebr_t;

// extended bpb, only for fat 32
typedef struct ebpb {
    uint32_t fat_sz_32;
    uint16_t ext_flags;
    uint16_t fs_ver;
    uint32_t root_clus;
    uint16_t fs_info;
    uint16_t bk_boot_sec;
    uint8_t  reserved[8];
} PACKED ebpb_t;

// boot sector for fat 12/16
typedef struct bs16 {
    uint8_t jmp_boot[3];
    uint8_t oem_name[8];
    bpb_t   bpb;
    ebr_t   ebr;
} PACKED bs16_t;

// boot sector for fat 32
typedef struct bs32 {
    uint8_t jmp_boot[3];
    uint8_t oem_name[8];
    bpb_t   bpb;
    ebpb_t  ebpb;
    ebr_t   ebr;
} PACKED bs32_t;

