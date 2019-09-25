#include <wheel.h>

// volume is the interface between block device and file system

typedef struct volume {
    dlnode_t    dl;
    blk_dev_t * blk;
    fs_t      * fs;             // must be NULL if vols not empty
    usize       sec_start;
    usize       sec_size;
} volume_t;

static usize vol_read(volume_t * vol, usize sec, usize n, u8 * buf) {
    n = MIN(n, vol->sec_size - sec);
    return blk_read(vol->parent, vol->sec_start + sec, n, buf);
}

static usize vol_write(volume_t * vol, usize sec, usize n, const u8 * buf) {
    n = MIN(n, vol->sec_size - sec);
    return blk_write(vol->parent, vol->sec_start + sec, n, buf);
}

static const blk_ops_t vol_ops = {
    .read  = (blk_read_t)  vol_read,
    .write = (blk_write_t) vol_write,
};

static void vol_delete(volume_t * vol) {
    kmem_free(sizeof(volume_t), vol);
}

volume_t * vol_create(blk_dev_t * parent, usize sec_start, usize sec_size) {
    volume_t * vol = kmem_alloc(sizeof(volume_t));
    blk_dev_init(&vol->blk, vol_delete, &vol_ops, BLK_READ|BLK_WRITE);
    vol->parent    = kref_retain(parent);
    vol->sec_start = sec_start;
    vol->sec_size  = sec_size;
    return vol;
}