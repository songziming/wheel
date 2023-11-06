#include <wheel.h>

// volume is the interface between block device and file system

static void volume_delete(volume_t * vol) {
    kmem_free(sizeof(volume_t), vol);
}

volume_t * volume_create(blk_dev_t * blk, usize sec_start, usize sec_count) {
    volume_t * vol = kmem_alloc(sizeof(volume_t));
    vol->ref       = KREF_INIT(volume_delete);
    vol->dl        = DLNODE_INIT;
    vol->blk       = kref_retain(blk);
    vol->fs        = NULL;
    vol->sec_start = sec_start;
    vol->sec_count = sec_count;
    return vol;
}

usize volume_read(volume_t * vol, usize sec, usize n, u8 * buf) {
    assert(sec + n <= vol->sec_count);
    return blk_read(vol->blk, vol->sec_start + sec, n, buf);
}

usize volume_write(volume_t * vol, usize sec, usize n, const u8 * buf) {
    assert(sec + n <= vol->sec_count);
    return blk_write(vol->blk, vol->sec_start + sec, n, buf);
}

//------------------------------------------------------------------------------
// volume management

static dllist_t vol_list  = DLLIST_INIT;
static int      vol_count = 0;

int volume_count() {
    return vol_count;
}

void volume_regist(volume_t * vol) {
    assert(NULL == vol->fs);
    dl_push_tail(&vol_list, &vol->dl);
    ++vol_count;
}

volume_t * volume_get(int idx) {
    dlnode_t * node = vol_list.head;
    for (int i = 0; i < idx; ++i) {
        if (NULL == node) {
            return NULL;
        }
        node = node->next;
    }
    return PARENT(node, volume_t, dl);
}
