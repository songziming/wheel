#include <wheel.h>

// main and backup boot sector
typedef struct bs {
    u8  jump[3];
    u8  fsid[8];
    u8  zero[53];
    u64 part_off;   // media relative sector offset of this partition
    u64 volume_len; 
} __PACKED bs_t;

// sanity check
int exfat_check(u8 * sec) {
    bs_t * bs = (bs_t *) sec;

    if ((0xeb != bs->jump[0]) || (0x76 != bs->jump[1]) || (0x90 != bs->jump[2])) {
        dbg_print("jump field not valid.\n");
        return NO;
    }
    if (0 != strncmp((const char *) bs->fsid, "EXFAT   ", 8)) {
        dbg_print("fs name field not valid.\n");
        return NO;
    }
    for (int i = 0; i < 53; ++i) {
        if (0 != bs->zero[i]) {
            dbg_print("BPB should all be zero.\n");
            return NO;
        }
    }

    return YES;
}

void exfat_fs_init(blk_dev_t * dev) {
    static u8 sec[512];
    if (512 != dev->sec_size) {
        dbg_print("sector size is not 512.\n");
        return;
    }
    if (1 != blk_read(dev, 0, 1, sec)) {
        dbg_print("cannot read from disk.\n");
        return;
    }

    if (YES != exfat_check(sec)) {
        dbg_print("sanity check failed.\n");
        return;
    }
}
