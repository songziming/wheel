// 输入一个块设备，识别其分区表，创建多个块设备

#include <wheel.h>



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




//------------------------------------------------------------------------------
// 识别 GPT 分区表
//------------------------------------------------------------------------------

// 如果不存在 GPT 分区表则返回 1
static int check_gpt_partitions(blk_dev_t *blk) {
    ASSERT(NULL != blk);

    uint8_t *lba0 = kernel_heap_alloc(blk->sec_size);
    block_read(blk, lba0, 0, 1);

    if (0 == memcmp(lba0, "EFI PART", 8)) {
        klog("warning: GPT not supported!\n");
        kernel_heap_free(lba0);
        return 1;
    }

    kernel_heap_free(lba0);
    return 1;
}



//------------------------------------------------------------------------------
// 识别 MBR 分区表
//------------------------------------------------------------------------------

// 我们不能假定分区表一定存在，只要发现不合理取值，就认为没有分区表
// 所有检测都通过之后，才能创建分区设备，并注册到系统中

static inline int chs_is_nonzero(mbr_chs_t *chs) {
    return chs->head || chs->sector || chs->cylinder;
}

static void check_mbr_partitions(blk_dev_t *blk) {
    ASSERT(NULL != blk);

    // 读取引导扇区
    uint8_t *sec0 = kernel_heap_alloc(blk->sec_size);
    block_read(blk, sec0, 0, 1);

    // 将分区表复制出来，地址对齐，扇区空间可以释放
    mbr_entry_t entries[4];
    memcpy(entries, sec0 + 446, 64);
    kernel_heap_free(sec0);

    int bootable_id = -1; // 哪个分区是可引导的（最多一个）
    int extended_id = -1; // 哪个分区是扩展分区（最多一个）

    for (int i = 0; i < 4; ++i) {
        if (0 == entries[i].system_id) {
            continue; // 这个分区不存在
        }

        // 相关字段不能全零，若全零说明分区表不合法
        if (!chs_is_nonzero(&entries[i].start)) {
            klog("mbr invalid, entry %d start chs zero!\n", i);
            return;
        }
        if (!chs_is_nonzero(&entries[i].end)) {
            klog("mbr invalid, entry %d end chs zero!\n", i);
            return;
        }
        if (!entries[i].start_lba || !entries[i].nsectors) {
            klog("mbr invalid, entry %d LBA or size zero!\n", i);
            return;
        }

        // 如果这是扩展分区
        if ((5 == entries[i].system_id) || (15 == entries[i].system_id)) {
            if (-1 != extended_id) {
                klog("mbr invalid, more than one extended partition!\n");
                return;
            }
            extended_id = i;
            entries[i].system_id = 0;
            klog("warning: extended partitions not supported!\n");
            continue;
        }

        // 如果是可引导分区
        if (0x80 == entries[i].boot) {
            if (-1 != bootable_id) {
                klog("mbr invalid, more than one bootable partition!\n");
                return;
            }
            bootable_id = i;
        }

        // 检查前几个有效分区，扇区范围不能有重叠
        uint64_t i_start = entries[i].start_lba;
        uint64_t i_end = i_start + entries[i].nsectors;
        for (int j = 0; j < i; ++j) {
            if (0 == entries[j].system_id) {
                continue;
            }
            uint64_t j_start = entries[j].start_lba;
            uint64_t j_end = j_start + entries[j].nsectors;
            if ((j_start < i_start) && (i_start < j_end)) {
                klog("mbr invalid, overlapping partitions!\n");
                return;
            }
            if ((i_start < j_start) && (j_start < i_end)) {
                klog("mbr invalid, overlapping partitions!\n");
                return;
            }
        }
    }

    // 分区表合法，创建分区设备
    for (int i = 0; i < 4; ++i) {
        if (0 == entries[i].system_id) {
            continue;
        }

        partition_t *part = kernel_heap_alloc(sizeof(partition_t));
        part->raw = blk;
        part->start = entries[i].start_lba;
        part->blk.name = strmake("%s_part%d", blk->name, i + 1);
        part->blk.sec_size = blk->sec_size;
        part->blk.sec_num = entries[i].nsectors;
        add_block_device(&part->blk, &g_partition_driver);
    }
}



//------------------------------------------------------------------------------
// 接口函数
//------------------------------------------------------------------------------

// 识别分区表，每个分区创建一个块设备
void partitions_init(blk_dev_t *blk) {
    ASSERT(NULL != blk);

    // 首先应该检查 GPT 分区表，失败再检查 MBR
    if (check_gpt_partitions(blk)) {
        check_mbr_partitions(blk);
    }
}

static void partition_show(blk_dev_t *blk) {
    partition_t *part = containerof(blk, partition_t, blk);
    klog("%s, within %s, sec_start=%ld, sec_count=%ld\n",
        blk->name, part->raw->name, part->start, blk->sec_num);
}

static void partition_read(blk_dev_t *blk, void *dst, uint64_t sec, uint64_t nsecs) {
    partition_t *part = containerof(blk, partition_t, blk);
    block_read(part->raw, dst, part->start + sec, nsecs);
}

static void partition_write(blk_dev_t *blk, const void *src, uint64_t sec, uint64_t nsecs) {
    partition_t *part = containerof(blk, partition_t, blk);
    block_write(part->raw, src, part->start + sec, nsecs);
}

INIT_TEXT void partition_driver_init() {
    memset(&g_partition_driver, 0, sizeof(g_partition_driver));
    g_partition_driver.show  = partition_show;
    g_partition_driver.read  = partition_read;
    g_partition_driver.write = partition_write;
    register_block_driver(&g_partition_driver);
}
