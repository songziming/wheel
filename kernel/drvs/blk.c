#include <wheel.h>

// block device abstract interface and manager

static dllist_t blk_list  = DLLIST_INIT;
static int      blk_count = 0;

void blk_dev_regist(blk_dev_t * dev) {
    if (NULL != dev->fs) {
        panic("registering block dev with file system.\n");
    }
    dl_push_tail(&blk_list, &dev->dl);
    ++blk_count;
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
