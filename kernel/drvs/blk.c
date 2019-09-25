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
// public functions

static dllist_t blk_list  = DLLIST_INIT;
static int      blk_count = 0;

void blk_dev_init(blk_dev_t * dev, void * del, const blk_ops_t * ops, int mode) {
    dev->ref      = KREF_INIT(del);
    dev->dl       = DLNODE_INIT;
    dev->vols     = DLLIST_INIT;
    dev->fs       = NULL;
    dev->ops_mode = ((usize) ops & ~3UL) | (mode & 3UL);
}

extern int partprobe(blk_dev_t * blk);

void blk_vol_regist(blk_dev_t * dev) {
    if (NULL != dev->fs) {
        panic("registering block dev with file system.\n");
    }
    dl_push_tail(&blk_list, &dev->dl);
    ++blk_count;
}

void blk_dev_regist(blk_dev_t * dev) {
    if ((512 != dev->sec_size) || (YES != partprobe(dev))) {
        blk_vol_regist(dev);
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
