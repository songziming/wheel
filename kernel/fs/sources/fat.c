// FAT 文件系统，包括 FAT12/FAT16/FAT32，以及 exFAT、VFAT

#include <fat.h>
#include <wheel.h>


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


// exFAT 引导扇区格式
typedef struct exbs {
    uint8_t  jump[3];
    uint8_t  fsid[8];
    uint8_t  zero[53];
    uint64_t vol_offset;     // (media relative) sector offset of this volume
    uint64_t vol_length;     // size of this volume in sectors
    uint32_t fat_offset;     // (volume relative) sector offset of first fat
    uint32_t fat_length;     // size of each fat table in sectors

    uint32_t cluster_heap_offset;
    uint32_t cluster_count;
    uint32_t first_cluster_of_root_dir;
    uint32_t vol_serial_num;
    uint16_t fs_revision;
    uint16_t volume_flags;
    uint8_t  bytes_per_sec_shift;
    uint8_t  secs_per_cluster_shift;
    uint8_t  num_of_fat;
    uint8_t  drive_select;
    uint8_t  percent_in_use;
    uint8_t  reserved[7];
    uint8_t  boot_code[390];
    uint16_t boot_signature;
} PACKED exbs_t;



//------------------------------------------------------------------------------

// 判断 FAT 文件系统的具体类型
void fat_determine(uint8_t *sec0) {
    bs16_t *bs16 = (bs16_t *)sec0;
    bs32_t *bs32 = (bs32_t *)sec0;

    // 计算根目录区占据的扇区数量
    int root_ent_cnt = bs16->bpb.root_ent_cnt;
    int byts_per_sec = bs16->bpb.byts_per_sec;
    int root_sec_cnt = ((root_ent_cnt * 32) + (byts_per_sec - 1)) / byts_per_sec;

    // 计算数据区的扇区数
    int fat_sz = bs16->bpb.fat_sz_16;
    if (0 == fat_sz) {
        fat_sz = bs32->ebpb.fat_sz_32;
    }
    int tot_sec = bs16->bpb.tot_sec_16;
    if (0 == tot_sec) {
        tot_sec = bs32->bpb.tot_sec_32;
    }
    int rsvd_sec_cnt = bs16->bpb.rsvd_sec_cnt;
    int num_fats = bs16->bpb.num_fats;
    int data_sec = tot_sec - (rsvd_sec_cnt + num_fats * fat_sz - root_sec_cnt);

    // 计算簇个数
    int clus_cnt = data_sec / bs16->bpb.sec_per_clus;

    // 分析 FAT 类型
    if (clus_cnt < 4085) {
        klog("[fat] this is FAT-12.\n");
    } else if (clus_cnt < 65525) {
        klog("[fat] this is FAT-16.\n");
    } else {
        klog("[fat] this is FAT-32.\n");
    }
}

// 判断文件系统是不是 exFAT
int is_exfat(uint8_t *sec0) {
    exbs_t *bs = (exbs_t *)sec0;

    if ((0xeb != bs->jump[0]) || (0x76 != bs->jump[1]) || (0x90 != bs->jump[2])) {
        klog("jump field not valid.\n");
        return 0;
    }
    if (0 != strncmp((const char *) bs->fsid, "EXFAT   ", 8)) {
        klog("fs name field not valid.\n");
        return 0;
    }
    for (int i = 0; i < 53; ++i) {
        if (0 != bs->zero[i]) {
            klog("BPB should all be zero.\n");
            return 0;
        }
    }

    // if ((0 != bs->vol_offset) && (vol->sec_start != bs->vol_offset)) {
    if (0 != bs->vol_offset) {
        klog("[exfat] warning: PartitionOffset not compliant.\n");
    }

    klog("[exfat] fs create ok.\n");
    return 1;
}
