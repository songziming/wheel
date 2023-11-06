#include <wheel.h>

// typedef struct exfat {
//     ft_t    fs;
//     usize   sec_size;
//     usize   sec_count;
// } exfat_t;

// main and backup boot sector
typedef struct bootsec {
    u8  jump[3];
    u8  fsid[8];
    u8  zero[53];
    u64 vol_offset;     // (media relative) sector offset of this volume
    u64 vol_length;     // size of this volume in sectors
    u32 fat_offset;     // (volume relative) sector offset of first fat
    u32 fat_length;     // size of each fat table in sectors

    u32 cluster_heap_offset;
    u32 cluster_count;
    u32 first_cluster_of_root_dir;
    u32 vol_serial_num;
    u16 fs_revision;
    u16 volume_flags;
    u8  bytes_per_sec_shift;
    u8  secs_per_cluster_shift;
    u8  num_of_fat;
    u8  drive_select;
    u8  percent_in_use;
    u8  reserved[7];
    u8  boot_code[390];
    u16 boot_signature;
} __PACKED bootsec_t;

// main and backup boot sector
int parse_boot_sector(volume_t * vol, u8 * sec) {
    bootsec_t * bs = (bootsec_t *) sec;

    if ((0xeb != bs->jump[0]) || (0x76 != bs->jump[1]) || (0x90 != bs->jump[2])) {
        dbg_print("jump field not valid.\n");
        return ERROR;
    }
    if (0 != strncmp((const char *) bs->fsid, "EXFAT   ", 8)) {
        dbg_print("fs name field not valid.\n");
        return ERROR;
    }
    for (int i = 0; i < 53; ++i) {
        if (0 != bs->zero[i]) {
            dbg_print("BPB should all be zero.\n");
            return ERROR;
        }
    }

    if ((0 != bs->vol_offset) && (vol->sec_start != bs->vol_offset)) {
        dbg_print("[exfat] warning: PartitionOffset not compliant.\n");
    }

    // exfat_t * exfat = kmem_alloc(sizeof(exfat_t));
    // fs_init()

    dbg_print("[exfat] fs create ok.\n");
    return OK;
}

void exfat_fs_init(volume_t * vol) {
    // main boot region -> main boot sector
    static u8 sec[512];
    volume_read(vol, 0, 1, sec);

    parse_boot_sector(vol, sec);
}
