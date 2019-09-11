#include <wheel.h>

// bios parameter block, 25-byte
typedef struct bpb {
    u16 byts_per_sec;
    u8  sec_per_clus;
    u16 rsvd_sec_cnt;
    u8  num_fats;
    u16 root_ent_cnt;
    u16 tot_sec_16;
    u8  media;
    u16 fat_sz_16;
    u16 sec_per_trk;
    u16 num_heads;
    u32 hidd_sec;
    u32 tot_sec_32;
} __PACKED bpb_t;

// extended boot record
typedef struct ebr {
    u8  drv_num;
    u8  reserved;
    u8  boot_sig;
    u32 vol_id;
    u8  vol_label[11];
    u8  fs_type[8];
} __PACKED ebr_t;

// extended bpb, only for fat 32
typedef struct ebpb {
    u32 fat_sz_32;
    u16 ext_flags;
    u16 fs_ver;
    u32 root_clus;
    u16 fs_info;
    u16 bk_boot_sec;
    u8  reserved[8];
} __PACKED ebpb_t;

// boot sector for fat 12/16
typedef struct bs16 {
    u8      jmp_boot[3];
    u8      oem_name[8];
    bpb_t   bpb;
    ebr_t   ebr;
} __PACKED bs16_t;

// boot sector for fat 32
typedef struct bs32 {
    u8      jmp_boot[3];
    u8      oem_name[8];
    bpb_t   bpb;
    ebpb_t  ebpb;
    ebr_t   ebr;
} __PACKED bs32_t;

// determine fat type
void fat_determine(u8 * boot_sec) {
    bs16_t * bs16 = (bs16_t *) boot_sec;
    bs32_t * bs32 = (bs32_t *) boot_sec;

    // determine the count of sectors occupied by the root directory
    int root_ent_cnt = bs16->bpb.root_ent_cnt;
    int byts_per_sec = bs16->bpb.byts_per_sec;
    int root_sec_cnt = ((root_ent_cnt * 32) + (byts_per_sec - 1)) / byts_per_sec;

    // determine the count of sectors in the data region
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

    // determine the count of clusters
    int clus_cnt = data_sec / bs16->bpb.sec_per_clus;

    // determine fat type
    if (clus_cnt < 4085) {
        dbg_print("[fat] this is FAT-12.\n");
    } else if (clus_cnt < 65525) {
        dbg_print("[fat] this is FAT-16.\n");
    } else {
        dbg_print("[fat] this is FAT-32.\n");
    }
}

void fat_fs_init(blk_dev_t * dev) {
    static u8 sec[512];

    if (512 != dev->sec_size) {
        dbg_print("[fat] sector size is not 512.\n");
        return;
    }
    if (1 != blk_read(dev, 0, 1, sec)) {
        dbg_print("[fat] cannot read from disk.\n");
        return;
    }

    if ((0x55 == sec[510]) && (0xaa == sec[511])) {
        dbg_print("[fat] this disk is bootable.\n");
    }

    // sanity check

    fat_determine(sec);
}
