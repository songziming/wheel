#include <wheel.h>

//------------------------------------------------------------------------------
// volume device, just a proxy into another block device

typedef struct volume_dev {
    blk_dev_t   blk;
    blk_dev_t * parent;
    usize       sec_start;
    usize       sec_size;
} volume_dev_t;

static usize vol_read(volume_dev_t * vol, usize sec, usize n, u8 * buf) {
    n = MIN(n, vol->sec_size - sec);
    return blk_read(vol->parent, vol->sec_start + sec, n, buf);
}

static usize vol_write(volume_dev_t * vol, usize sec, usize n, const u8 * buf) {
    n = MIN(n, vol->sec_size - sec);
    return blk_write(vol->parent, vol->sec_start + sec, n, buf);
}

static const blk_ops_t vol_ops = {
    .read  = (blk_read_t)  vol_read,
    .write = (blk_write_t) vol_write,
};

static void vol_delete(volume_dev_t * vol) {
    kmem_free(sizeof(volume_dev_t), vol);
}

volume_dev_t * vol_create(blk_dev_t * parent, usize sec_start, usize sec_size) {
    volume_dev_t * vol = kmem_alloc(sizeof(volume_dev_t));
    blk_dev_init(&vol->blk, vol_delete, &vol_ops, BLK_READ|BLK_WRITE);
    vol->parent    = kref_retain(parent);
    vol->sec_start = sec_start;
    vol->sec_size  = sec_size;
    return vol;
}

//------------------------------------------------------------------------------
// parse partition table, create volume devices

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
    return 63 * (16 * c + h) + s - 1;
}

static int partition_check(partition_t * part, blk_dev_t * parent) {
    int valid_count  = 0;
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
        u32 * ent = (u32 *) &part[i];
        if ((0 == ent[0]) && (0 == ent[1]) && (0 == ent[2]) && (0 == ent[3])) {
            dbg_print("[mbr] skip invalid entry %d.\n", i);
            continue;
        } else {
            ++valid_count;
        }

        if (0x80 == part[i].state) {
            ++active_count;
            dbg_print("[mbr] %d. partition active (28) ", i);
        } else if (0x00 == part[i].state) {
            dbg_print("[mbr] %d. partition normal (28): ", i);
        } else if (0x81 == part[i].state) {
            ++active_count;
            dbg_print("[mbr] %d. partition bootable (48): ", i);
        } else if (0x01 == part[i].state) {
            dbg_print("[mbr] %d. partition non-bootable (48): ", i);
        } else {
            return NO;
        }

        volume[i].sec_start = part[i].lba_start;
        volume[i].sec_size  = part[i].lba_size;
        dbg_print("range 0x%llx:0x%llx.\n", volume[i].sec_start, volume[i].sec_size);

        // start and last sector CHS
        u16 start_c = (((u16) part[i].start_chs[1] << 2) & 0x0300) | ((u16) part[i].start_chs[2]);
        u8  start_h = part[i].start_chs[0];
        u8  start_s = part[i].start_chs[1] & 0x3f;
        u16 last_c = (((u16) part[i].last_chs[1] << 2) & 0x0300) | ((u16) part[i].last_chs[2]);
        u8  last_h = part[i].last_chs[0];
        u8  last_s = part[i].last_chs[1] & 0x3f;
        dbg_print("[mbr]    start chs %d/%d/%d, last chs %d/%d/%d.\n", start_c, start_h, start_s, last_c, last_h, last_s);

        // // sector number must be non-zero
        // if ((0 == start_s) || (0 == last_s)) {
        //     dbg_print("[mbr] sector number is zero!\n");
        //     return NO;
        // }

        // // if smaller than 8G, lba and chs must match
        // if (part[i].lba_size < 0x02000000) {
        //     u32 lba_start = chs_to_lba(start_c, start_h, start_s);
        //     u32 lba_last  = chs_to_lba(last_c,  last_h,  last_s);
        //     if (part[i].lba_start != lba_start) {
        //         dbg_print("[mbr] start lba and chs not match!\n");
        //         return NO;
        //     }
        //     if (part[i].lba_size != (lba_last - lba_start + 1)) {
        //         dbg_print("[mbr] last lba and chs not match! %d %d\n", part[i].lba_size, (lba_last - lba_start + 1));
        //         return NO;
        //     }
        // }
    }

    if (active_count > 1) {
        dbg_print("[mbr] more than one partition is marked bootable!\n");
        return NO;
    }

    // loop volumes again, create block device object and register
    // TODO: check for extended partition
    for (int i = 0; i < 4; ++i) {
        if ((0 == volume[i].sec_start) && (0 == volume[i].sec_size)) {
            continue;
        }
        // create volume object, and regist without checking for partition again
        volume_dev_t * vol = vol_create(parent, volume[i].sec_start, volume[i].sec_size);
        blk_dev_regist_no_partition((blk_dev_t *) vol);
    }

    return YES;
}

//------------------------------------------------------------------------------
// public functions

void blk_dev_init(blk_dev_t * dev, void * del, const blk_ops_t * ops, int mode) {
    dev->ref      = KREF_INIT(del);
    dev->dl       = DLNODE_INIT;
    dev->ops_mode = ((usize) ops & ~3UL) | (mode & 3UL);
    dev->fs       = NULL;
}

void blk_dev_regist_no_partition(blk_dev_t * dev) {
    if (NULL != dev->fs) {
        panic("registering block dev with file system.\n");
    }
    dl_push_tail(&blk_list, &dev->dl);
    ++blk_count;
}

void blk_dev_regist(blk_dev_t * dev) {
    blk_dev_regist_no_partition(dev);

    if (512 != dev->sec_size) {
        return;
    }

    static u8 mbr[512];
    blk_read(dev, 0, 1, mbr);

    if (YES != partition_check((partition_t *) &mbr[446], dev)) {
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
