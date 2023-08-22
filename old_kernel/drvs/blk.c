#include <wheel.h>

//------------------------------------------------------------------------------
// public functions

void blk_dev_init(blk_dev_t * dev, void * del, const blk_ops_t * ops, int mode) {
    dev->ref       = KREF_INIT(del);
    dev->dl        = DLNODE_INIT;
    dev->ops_mode  = ((usize) ops & ~3UL) | (mode & 3UL);
    dev->sec_size  = 0;
    dev->sec_count = 0;
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

//------------------------------------------------------------------------------
// block device management

static dllist_t blk_list  = DLLIST_INIT;
static int      blk_count = 0;

int blk_dev_count() {
    return blk_count;
}

void blk_dev_regist(blk_dev_t * dev) {
    assert(0 != dev->sec_size);
    assert(0 != dev->sec_count);
    dl_push_tail(&blk_list, &dev->dl);
    ++blk_count;
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
