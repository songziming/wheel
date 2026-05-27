#include "block.h"

#include <console.h>
#include <kshell.h>


// 管理所有块设备
static dlnode_t g_block_head;



INIT_TEXT void block_dev_init() {
    dl_init_circular(&g_block_head);
}

void add_block_dev(block_dev_t *dev) {
    dl_insert_before(&dev->dl, &g_block_head);
}

void block_read(block_dev_t *dev, void *dst, uint32_t sec, uint32_t nsecs) {
    dev->ops->read(dev, dst, sec, nsecs);
}

void block_write(block_dev_t *dev, const void *src, uint32_t sec, uint32_t nsecs) {
    dev->ops->write(dev, src, sec, nsecs);
}


static void show_blocks() {
    console_printf("block devices:\n");
    dlnode_t *dl = g_block_head.next;
    for (; dl != &g_block_head; dl = dl->next) {
        block_dev_t *blk = containerof(dl, block_dev_t, dl);
        console_printf(" -> %s, blk_size=%u, blk_count=%lu\n",
            blk->name, blk->sec_size, blk->sec_num);
    }
}

KSHELL_CMD("blocks", show_blocks);
