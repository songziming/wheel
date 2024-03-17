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



void partitions_init(blk_dev_t *blk) {
    ASSERT(NULL != blk);

    // 读取引导扇区
    uint8_t *sec0 = kernel_heap_alloc(blk->sec_size);
    block_read(blk, sec0, 0, 1);

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
        part->raw = blk;
        part->start = parts[i].start_lba;
        part->blk.name = strmake("%s_part%d", blk->name, i + 1);
        part->blk.sec_size = blk->sec_size;
        part->blk.sec_num = parts[i].nsectors;
        add_block_device(&part->blk, &g_partition_driver);
    }

    // 如果有扩展分区，还要递归遍历，编号从 4 开始
    if (-1 != ext_id) {
        //
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
