// 输入一个块设备，识别其分区表，创建多个块设备
// 新创建的块设备带有缓存能力

#include <wheel.h>


//------------------------------------------------------------------------------
// 带有缓存的块设备
//------------------------------------------------------------------------------

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

static blk_drv_t g_cached_drv;



cached_block_t *cached_block_create(blk_dev_t *blk) {
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
    add_block_device(&cached->blk, &g_cached_drv);
    return cached;
}

// TODO 一次可能读取多个扇区，应该把需要连续读，连续写回的扇区操作合并
static void cached_read(blk_dev_t *dev, void *dst, uint64_t blk, uint32_t nblk) {
    cached_block_t *cached = containerof(dev, cached_block_t, blk);
    uint8_t *buff = (uint8_t *)dst;
    uint32_t sec_size = dev->sec_size;

    for (uint32_t i = 0; i < nblk; ++i) {
        int id = (blk + i) & 255;
        uint64_t tag = (blk + i) & ~255UL;
        uint64_t sec = cached->cached_sec[id];

        if (CACHE_VALID & sec) {
            if (tag == (sec & ~255UL)) {
                // 命中，直接从缓存 copy
            } else if (CACHE_DIRTY & sec) {
                // 首先把这个扇区写回
            }
        }

        // 从底层块设备读取数据，放在缓存里
        block_read(cached->uncached, buff, blk + i, 1);
        cached->cached_sec[id] = tag | CACHE_VALID;
        memcpy(cached->cached_data + id * sec_size, buff, sec_size);
    }
}

static void cached_write(blk_dev_t *dev, const void *src, uint64_t blk, uint32_t nblk) {
    //
}

void cached_block_init() {
    g_cached_drv.read = cached_read;
    g_cached_drv.write = cached_write;
    register_block_driver(&g_cached_drv);
}


//------------------------------------------------------------------------------
// 逻辑卷
//------------------------------------------------------------------------------

// 一个分区也是一个块设备
typedef struct partition {
    blk_dev_t   blk;
    blk_dev_t  *raw;    // 来自哪个块设备
    uint64_t    start;  // 起始扇区号
} partition_t;


typedef struct mbr_chs {
    uint8_t  head;
    uint8_t  sector;
    uint8_t  cylinder;
} PACKED mbr_chs_t;

typedef struct mbr_entry {
    uint8_t   boot;   // 0x80 表示可引导
    mbr_chs_t start;
    uint8_t   system_id;
    mbr_chs_t end;
    uint32_t  start_lba;
    uint32_t  nsectors;
} PACKED mbr_entry_t;

static blk_drv_t g_partition_driver;

void partitions_init(blk_dev_t *dev) {
    ASSERT(NULL != dev);

    // 读取引导扇区
    uint8_t *sec0 = kernel_heap_alloc(dev->sec_size);
    block_read(dev, sec0, 0, 1);

    // 检查 MBR 分区表
    mbr_entry_t *parts = (mbr_entry_t *)(sec0 + 446);
    // int valid_num = 0;
    // int boot_num = 0;
    int ext_id = -1;
    for (int i = 0; i < 4; ++i) {
        if (0 == parts[i].system_id) {
            continue;
        }
        if ((0 == parts[i].start.head) && (0 == parts[i].start.sector) && (0 == parts[i].start.cylinder)) {
            continue;
        }
        if ((0 == parts[i].end.head) && (0 == parts[i].end.sector) && (0 == parts[i].end.cylinder)) {
            continue;
        }
        if (0 == parts[i].nsectors) {
            continue;
        }

        // 如果这是扩展分区
        if ((5 == parts[i].system_id) || (15 == parts[i].system_id)) {
            ext_id = i;
            continue;
        }

        // ++valid_num;
        // if (0x80 == parts[i].boot) {
        //     ++boot_num;
        // }

        partition_t *part = kernel_heap_alloc(sizeof(partition_t));
        part->raw = dev;
        part->start = parts[i].start_lba;
        part->blk.name = strmake("%s_part%d", dev->name, i);
        part->blk.sec_size = dev->sec_size;
        part->blk.sec_num = parts[i].nsectors;
        add_block_device(&part->blk, &g_partition_driver);
    }

    // 如果有扩展分区，还要递归遍历，编号从 4 开始
    if (-1 != ext_id) {
        //
    }
}

static void partition_read(blk_dev_t *dev, void *dst, uint64_t blk, uint32_t nblk) {
    partition_t *part = containerof(dev, partition_t, blk);
    block_read(part->raw, dst, part->start + blk, nblk);
}

static void partition_write(blk_dev_t *dev, const void *src, uint64_t blk, uint32_t nblk) {
    partition_t *part = containerof(dev, partition_t, blk);
    block_write(part->raw, src, part->start + blk, nblk);
}

INIT_TEXT void partition_driver_init() {
    g_partition_driver.read = partition_read;
    g_partition_driver.write = partition_write;
    register_block_driver(&g_partition_driver);
}
