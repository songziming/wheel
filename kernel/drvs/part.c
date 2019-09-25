#include <wheel.h>

// TODO: add support for extended/logical partitions
// TODO: add support for GPT format

// mbr partition entry format
typedef struct mbr_part {
    u8  state;
    u8  start_chs[3];
    u8  sysid;
    u8  last_chs[3];
    u32 lba_start;
    u32 lba_size;
} __PACKED mbr_part_t;

static inline u32 chs_to_lba(u16 c, u8 h, u8 s) {
    return 63 * (16 * c + h) + s - 1;
}

static int probe(blk_dev_t * blk, usize start, usize size) {
    static u8 sec[512];
    blk_read(blk, start, 1, sec);

    // TODO: at most one bootable partition
    // TODO: at most one extended partition (sysid = 5 or 0xf)

    mbr_part_t * parts = (mbr_part_t *) &sec[446];
    for (int i = 0; i < 4; ++i) {
        u8  start_s = part->start_chs[1] & 0x3f;
        u8  last_s  = part->last_chs[1]  & 0x3f;
        if ((0 == start_s) || (0 == last_s)) {
            continue;
        }
        if ((0 == part->sysid) || (0 == part->lba_size)) {
            continue;
        }
        if (parts[i].lba_start + parts[i].lba_size > size) {
            continue;
        }

        if (0x80 == parts[i].state) {
            dbg_print("[mbr] %d. partition active (28): ", i);
        } else if (0x00 == parts[i].state) {
            dbg_print("[mbr] %d. partition normal (28): ", i);
        } else {
            return NO;
        }
        dbg_print("range 0x%llx:0x%llx, ", start + parts[i].lba_start, start + parts[i].lba_size);

        dbg_print("type %x.\n", parts[i].sysid);
        if ((0x05 == parts[i].sysid) || (0x0f == parts[i].sysid)) {
            // TODO: parse extended volume
        } else {
            // TODO: add volume object
        }
    }

    return YES;
}

int partprobe(blk_dev_t * blk) {
    return probe(blk, 0, blk->sec_size);
}
