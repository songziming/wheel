// 带有缓存的块设备

#include <wheel.h>



// 可以缓存 256 个扇区，按照扇区号的最末 8-bit 索引
typedef struct cached_block {
    blk_dev_t   blk;
    blk_dev_t  *uncached;
    uint64_t    cached_sec[256];
    uint8_t    *cached_data;
} cached_block_t;

#define CACHE_VALID     1   // 该扇区的缓存存在
#define CACHE_DIRTY     2   // 缓存的数据已更新，未写入磁盘
#define CACHE_KEY(blk, id)  ((blk)->cached_sec[id] & ~255UL)    // 缓存的扇区号

static blk_drv_t g_cached_driver;



blk_dev_t *cached_block_create(blk_dev_t *blk) {
    cached_block_t *cached = kernel_heap_alloc(sizeof(cached_block_t));
    if (NULL == cached) {
        return NULL;
    }

    context_t *ctx = THISCPU_GET(g_tid_prev)->process;
    int page_num = (blk->sec_size * 256) >> PAGE_SHIFT;
    int rank = __builtin_ctz(page_num);

    char *name = strmake("cache %s", blk->name);
    cached->cached_data = context_alloc(ctx, rank, PT_CACHE, MMU_WRITE, name);
    if (NULL == cached->cached_data) {
        klog("cannot allocate cache buffer for %s\n", blk->name);
        kernel_heap_free(name);
        kernel_heap_free(cached);
        return NULL;
    }

    cached->uncached = blk;
    memset(cached->cached_sec, 0, 256 * sizeof(uint64_t));
    add_block_device(&cached->blk, &g_cached_driver);
    return &cached->blk;
}

// TODO 一次可能读取多个扇区，应该把需要连续读，连续写回的扇区操作合并
static void cached_read(blk_dev_t *blk, void *dst, uint64_t start, uint64_t nsecs) {
    cached_block_t *cached = containerof(blk, cached_block_t, blk);
    uint32_t sec_size = blk->sec_size;
    uint8_t *data = (uint8_t *)dst;
    uint8_t *temp = cached->cached_data + start * sec_size;

    for (uint32_t i = 0; i < nsecs; ++i) {
        uint64_t id = (start + i) & 255;
        uint64_t tag = (start + i) & ~255UL;
        uint64_t sec = cached->cached_sec[id];

        if (CACHE_VALID & sec) {
            if (tag == (sec & ~255UL)) {
                // 命中，直接从缓存 copy
                memcpy(data, temp, sec_size);
                goto next;
            } else if (CACHE_DIRTY & sec) {
                // 首先把缓存的扇区写回
                uint64_t old_sec = (sec & ~255UL) | id;
                uint8_t *old_temp = cached->cached_data + old_sec * sec_size;
                block_write(cached->uncached, old_temp, old_sec, 1);
            }
        }

        // 从底层块设备读取数据，放在缓存里
        block_read(cached->uncached, data, start + i, 1);
        cached->cached_sec[id] = tag | CACHE_VALID;
        memcpy(temp, data, sec_size);
next:
        temp += sec_size;
        data += sec_size;
    }
}

static void cached_write(blk_dev_t *blk, const void *src, uint64_t start, uint64_t nsecs) {
    cached_block_t *cached = containerof(blk, cached_block_t, blk);
    uint32_t sec_size = blk->sec_size;
    const uint8_t *data = (const uint8_t *)src;
    uint8_t *temp = cached->cached_data + start * sec_size;

    for (uint32_t i = 0; i < nsecs; ++i) {
        uint64_t id = (start + i) & 255;
        uint64_t tag = (start + i) & ~255UL;
        uint64_t sec = cached->cached_sec[id];

        if (CACHE_VALID & sec) {
            if (tag == (sec & ~255UL)) {
                // 命中，直接更新缓存
                memcpy(cached->cached_data + id * sec_size, data, sec_size);
                cached->cached_sec[id] |= CACHE_DIRTY;
                goto next;
            } else if (CACHE_DIRTY & sec) {
                // 未命中，需要写回
                // 这里有两个选择，将当前缓存的 sector 写回，还是把本次的 sector 写回？
                // 这里选择的是 write-back
                uint64_t old_sec = (sec & ~255UL) | id;
                uint8_t *old_temp = cached->cached_data + old_sec * sec_size;
                block_write(cached->uncached, old_temp, old_sec, 1);
            }
        }

        // 将数据写在缓存里，而不是设备里
        memcpy(temp, data, sec_size);
        cached->cached_sec[id] = tag | CACHE_VALID | CACHE_DIRTY;
next:
        data += sec_size;
        temp += sec_size;
    }
}

INIT_TEXT void cached_block_driver_init() {
    memset(&g_cached_driver, 0, sizeof(g_cached_driver));
    g_cached_driver.read = cached_read;
    g_cached_driver.write = cached_write;
    register_block_driver(&g_cached_driver);
}
