#include "partition.h"
#include <vmspace.h>
#include <page.h>
#include <arch_config.h>
#include <heap.h>
#include <kstring.h>
#include <debug.h>

// 某些块设备由分区表，可以分成多个（逻辑）块设备

// 一个分区也是一个块设备
typedef struct partition {
    block_dev_t  blk;
    block_dev_t *raw;    // 来自哪个块设备
    uint64_t     start;  // 起始扇区号
} partition_t;


static void part_read(block_dev_t *dev, void *dst, uint64_t sec, uint64_t nsecs) {
    partition_t *part = containerof(dev, partition_t, blk);
    block_read(part->raw, dst, part->start + sec, nsecs);
}

static void part_write(block_dev_t *dev, const void *src, uint64_t sec, uint64_t nsecs) {
    partition_t *part = containerof(dev, partition_t, blk);
    block_write(part->raw, src, part->start + sec, nsecs);
}

static const block_ops_t part_ops = {
    .read = part_read,
    .write = part_write,
};





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

static inline int chs_is_nonzero(mbr_chs_t *chs) {
    return chs->head || chs->sector || chs->cylinder;
}

// 输入一个块设备，检查分区表是否存在，每个分区都创建一个块设备
void partition_init(block_dev_t *raw) {
    if (512 != raw->sec_size) {
        // TODO 应该处理扇区大于 512 的情况
        logk("%s sec size is not 512\n", raw->name);
        return;
    }

    char blk[512];
    block_read(raw, blk, 0, 1);
    if (0 == kmemcmp(blk, "EFI PART", 8)) {
        logk("%s has GPT-partition! not supported!\n", raw->name);
        // vmspace_remove(&g_kernel_vm, &tmp_sec);
        return;
    }

    // MBR 分区表位于引导扇区末尾，没有按结构体对齐
    // 我们需要拷贝出来访问，否则会出现 unaligned-access
    mbr_entry_t entries[4];
    kmemcpy(entries, blk + 446, 64);
    // vmspace_remove(&g_kernel_vm, &tmp_sec);

    int bootable_id = -1; // 哪个分区是可引导的（最多一个）
    int extended_id = -1; // 哪个分区是扩展分区（最多一个）

    // 解析 MBR 分区表
    for (int i = 0; i < 4; ++i) {
        if (0 == entries[i].system_id) {
            continue; // 这个分区不存在
        }

        // 相关字段不能全零，若全零说明分区表不合法
        if (!chs_is_nonzero(&entries[i].start)) {
            logk("mbr invalid, entry %d start chs zero!\n", i);
            return;
        }
        if (!chs_is_nonzero(&entries[i].end)) {
            logk("mbr invalid, entry %d end chs zero!\n", i);
            return;
        }
        if (!entries[i].start_lba || !entries[i].nsectors) {
            logk("mbr invalid, entry %d LBA or size zero!\n", i);
            return;
        }

        // 如果这是扩展分区
        if ((5 == entries[i].system_id) || (15 == entries[i].system_id)) {
            if (-1 != extended_id) {
                logk("mbr invalid, more than one extended partition!\n");
                return;
            }
            extended_id = i;
            entries[i].system_id = 0;
            logk("warning: extended partitions not supported!\n");
            continue;
        }

        // 如果是可引导分区
        if (0x80 == entries[i].boot) {
            if (-1 != bootable_id) {
                logk("mbr invalid, more than one bootable partition!\n");
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
                logk("mbr invalid, overlapping partitions!\n");
                return;
            }
            if ((i_start < j_start) && (j_start < i_end)) {
                logk("mbr invalid, overlapping partitions!\n");
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
        part->raw = raw;
        part->start = entries[i].start_lba;
        part->blk.ops = &part_ops;
        part->blk.name = kernel_heap_mkstr("%sp%d", raw->name, i + 1);
        part->blk.sec_size = raw->sec_size;
        part->blk.sec_num = entries[i].nsectors;
        add_block_dev(&part->blk);
    }
}
