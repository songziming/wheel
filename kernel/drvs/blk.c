#include <wheel.h>

// block device abstract interface and manager

static dllist_t blk_list  = DLLIST_INIT;
static int      blk_count = 0;

typedef struct partition {
    u8  state;
    u8  start_chs[3];
    u8  sysid;
    u8  last_chs[3];
    u32 lba_start;
    u32 lba_size;
} __PACKED partition_t;

static inline u32 chs_to_lba(u16 c, u8 h, u8 s) {
    u32 lba = 0;
    lba  += c;
    lba <<= 8+6;
    lba  += h;
    lba <<= 6;
    lba  += s-1;
    return lba;
    //
    c * 
}

static int partition_check(partition_t * part) {
    int active_count = 0;

    struct {
        usize sec_start;
        usize sec_size;
    } volume[4];

    // fill them as 0
    for (int i = 0; i < 4; ++i) {
        volume[i].sec_start = 0;
        volume[i].sec_size  = 0;
    }

    for (int i = 0; i < 4; ++i) {
        if (0x80 == part[i].state) {
            ++active_count;
        } else if (0x00 != part[i].state) {
            return NO;
        }
        if ((0 == part[i].lba_start) && (0 == part[i].lba_size)) {
            continue;
        }

        volume[i].sec_start = part[i].lba_start;
        volume[i].sec_size  = part[i].lba_size;

        // CHS address, sector must be zero
        u16 start_c = (((u16) part[i].start_chs[1] << 2) & 0x0300) | ((u16) part[i].start_chs[2]);
        u8  start_h = part[i].start_chs[0];
        u8  start_s = part[i].start_chs[1] & 0x3f;
        if (0 == start_s) {
            return NO;
        }

        // if smaller than 8G, lba and chs must match
        if (part[i].lba_size < 0x02000000) {
            if (part[i].lba_size != chs_to_lba(start_c, start_h, start_s)) {
                return NO;
            }
        }
    }

    return YES;
}

void blk_dev_regist(blk_dev_t * dev) {
    if (NULL != dev->fs) {
        panic("registering block dev with file system.\n");
    }
    dl_push_tail(&blk_list, &dev->dl);
    ++blk_count;

    // now check the partition table
    static u8 mbr[512];
    if (512 != dev->sec_size) {
        return;
    }
    blk_read(dev, 0, 1, mbr);

    if (YES != partition_check((partition_t *) &mbr[446])) {
        dbg_print("no partition table found, using the disk as one volume.\n");
    }
}

int blk_count_get() {
    return blk_count;
}

blk_dev_t * blk_dev_get(int idx) {
    dlnode_t * node = blk_list.head;
    for (int i = 0; i < idx; ++i) {
        if (NULL == node) {
            return NULL;
        }
        node = node->next;
    }
    return PARENT(node, blk_dev_t, dl);
}

usize blk_read(blk_dev_t * dev, usize sec, usize n, void * buf) {
    if (dev->ops_mode & BLK_READ) {
        return BLK_OPS(dev)->read(dev, sec, n, (u8 *) buf);
    } else {
        return (usize) -1;
    }
}

usize blk_write(blk_dev_t * dev, usize sec, usize n, const void * buf) {
    if (dev->ops_mode & BLK_WRITE) {
        return BLK_OPS(dev)->write(dev, sec, n, (const u8 *) buf);
    } else {
        return (usize) -1;
    }
}
