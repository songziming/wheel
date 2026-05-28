#include "fs_fat.h"
#include <arch_config.h>
#include <debug.h>
#include <vmspace.h>

#include "console.h"
#include "kshell.h"


// FAT16/FAT32 文件系统，不支持 exFAT
// 只能读文件，没有缓存优化

// 整块 partition 分为四部分：
//  1. 保留区，包括引导扇区，里面有 BPB 数据，是文件系统的超级块
//  2. FAT 区（单链表，两份），每个 FAT-entry 描述一个簇
//  3. 根目录区（FAT32 没有此区域）
//  4. 数据区

// 将文件内容拆分成若干簇，一个簇就是若干扇区
// 一个文件的簇不必连续，FAT 存储每个簇的后继簇编号

// bios parameter block, 25-byte
typedef struct bpb {
    uint16_t byts_per_sec;  // 扇区大小 512/1024/2048/4096
    uint8_t  sec_per_clus;  // 簇是最小分配单位，必须是 2 的幂，一个簇不能大于 32K
    uint16_t rsvd_sec_cnt;  // FAT12/FAT16 通常取 1，FAT32 通常取 32
    uint8_t  num_fats;      // 有多少个 FAT，通常取 2，防止扇区损坏
    uint16_t root_ent_cnt;  // 根目录区里面，32-byte 条目的数量（FAT32 为 0）
    uint16_t tot_sec_16;    // 总扇区数
    uint8_t  media;
    uint16_t fat_sz_16;     // 一个 FAT 占据多少扇区
    uint16_t sec_per_trk;
    uint16_t num_heads;
    uint32_t hidd_sec;
    uint32_t tot_sec_32;
} PACKED bpb_t;

// extended boot record
typedef struct ebr {
    uint8_t  drv_num;
    uint8_t  reserved;
    uint8_t  boot_sig;
    uint32_t vol_id;
    uint8_t  vol_label[11];
    uint8_t  fs_type[8];
} PACKED ebr_t;

// extended bpb, only for fat 32
typedef struct ebpb {
    uint32_t fat_sz_32;
    uint16_t ext_flags;
    uint16_t fs_ver;
    uint32_t root_clus;
    uint16_t fs_info;
    uint16_t bk_boot_sec;
    uint8_t  reserved[8];
} PACKED ebpb_t;

// FAT12/FAT16 引导扇区格式
typedef struct bs16 {
    uint8_t jmp_boot[3];
    uint8_t oem_name[8];
    bpb_t   bpb;
    ebr_t   ebr;
} PACKED bs16_t;

// FAT32 引导扇区格式
typedef struct bs32 {
    uint8_t jmp_boot[3];
    uint8_t oem_name[8];
    bpb_t   bpb;
    ebpb_t  ebpb;
    ebr_t   ebr;
} PACKED bs32_t;


// 文件系统子类
typedef struct fs_fat {
    sys_fs_t     fs;
    block_dev_t *blk;

    // BPB 里面的重要数据提取出来
    char        fat_type;  // 12|16|32
    int         sec_size;
    int         rsvd_secs;  // 保留区扇区数
    int         fat_secs;   // 一个 FAT 占据的扇区
    int         fat_num;    // FAT 数量
    int         root_ents;  // 根目录条目数
    int         root_secs;  // 根目录区扇区数
    int         total_secs; // 总扇区数
    int         data_secs;  // 数据区扇区数
    int         clus_cnt;   // （数据区）簇个数
    int         clus_size;  // 一个簇对应多少个扇区
    int         clus_rank;  // 一个簇对应的页块大小

    // fat32
    uint32_t    root_clus;  // 根目录区所在的簇编号

    vmrange_t   rng_fat;
    uint16_t   *fat16;
    uint32_t   *fat32;
    vmrange_t   rng_clus;
} fs_fat_t;


//------------------------------------------------------------------------------
// 判断文件系统类型，检查是不是 FAT，是何种 FAT
//------------------------------------------------------------------------------

// TODO fschk 函数应该对外公开，返回 fs_ops 指针
void fat_determine(fs_fat_t *fs, char *bs) {
    bs16_t *bs16 = (bs16_t*)bs;
    bs32_t *bs32 = (bs32_t*)bs;

    // 计算根目录区占据的扇区数量，向上取整（FAT32 一定是零）
    fs->root_ents = bs16->bpb.root_ent_cnt;
    fs->sec_size  = bs16->bpb.byts_per_sec;
    fs->root_secs = (fs->root_ents * 32 + fs->sec_size - 1) / fs->sec_size;

    // 计算数据区的扇区数，总扇区数减去其他区域
    fs->fat_secs = bs16->bpb.fat_sz_16;
    if (0 == fs->fat_secs) {
        fs->fat_secs = bs32->ebpb.fat_sz_32;
    }
    fs->total_secs = bs16->bpb.tot_sec_16;
    if (0 == fs->total_secs) {
        fs->total_secs = bs32->bpb.tot_sec_32;
    }
    fs->rsvd_secs = bs16->bpb.rsvd_sec_cnt;
    fs->fat_num = bs16->bpb.num_fats;
    fs->data_secs = fs->total_secs - (fs->rsvd_secs + fs->fat_num * fs->fat_secs + fs->root_secs);

    // 数据区的扇区组成簇，计算簇个数（向下取整）
    fs->clus_cnt = fs->data_secs / bs16->bpb.sec_per_clus;

    // 数据簇的数量决定了 FAT 类型
    if (fs->clus_cnt < 4085) {
        fs->fat_type = 12;
    } else if (fs->clus_cnt < 65525) {
        fs->fat_type = 16;
    } else {
        fs->fat_type = 32;
        fs->root_clus = bs32->ebpb.root_clus;
    }
    logk("[fat] this is FAT-%d.\n", fs->fat_type);

    // cluster 大小，必然小于 32K，也可能小于一个页
    fs->clus_size = bs16->bpb.sec_per_clus;
    if (fs->clus_size * fs->sec_size < PAGE_SIZE) {
        fs->clus_rank = 0;
    } else {
        fs->clus_rank = __builtin_ctz(fs->clus_size * fs->sec_size >> PAGE_SHIFT);
    }

    // 将完整 FAT 读取出来，我们只读取一个 FAT
    int fat_pages = fs->fat_secs * fs->sec_size;
    fat_pages += PAGE_SIZE - 1;
    fat_pages >>= PAGE_SHIFT;
    logk("%d pages for one FAT table\n", fat_pages);

    if (fat_pages & (fat_pages - 1)) {
        fat_pages <<= 1;
        while (fat_pages & (fat_pages - 1)) {
            fat_pages &= fat_pages - 1;
        }
    }

    // 分配 FAT 的空间
    int fat_rank = __builtin_ctz(fat_pages);
    logk("allocating rank-%d for FAT\n", fat_rank);
    size_t fat_va = vmspace_alloc(&g_kernel_vm, &fs->rng_fat,
        POOL_ZONE_START, POOL_ZONE_END, fat_rank, PT_FS, MMU_WRITE);
    fs->rng_fat.desc = "FAT table cache";
    fs->fat16 = (uint16_t*)fat_va;
    fs->fat32 = (uint32_t*)fat_va;
    if (0 == fat_va) {
        logk("cannot allocate space for FAT\n");
        return;
    }
    logk("allocated fat cache at %p\n", fs->fat16);

    // 将 FAT 读取出来
    block_read(fs->blk, fs->fat16, fs->rsvd_secs, fs->fat_secs);
    logk("read %d sectors of FAT\n", fs->fat_secs);

    // 分配 cluster 空间，用于读文件
    logk("allocating rank-%d for cluster\n", fs->clus_rank);
    size_t clusva = vmspace_alloc(&g_kernel_vm, &fs->rng_clus,
        POOL_ZONE_START, POOL_ZONE_END, fs->clus_rank, PT_FS, MMU_WRITE);
    fs->rng_clus.desc = "FAT cluster cache";
    logk("allocated cluster cache at %zx %zx\n", clusva, fs->rng_clus.vaddr);
}


//------------------------------------------------------------------------------
// 文件系统操作
//------------------------------------------------------------------------------

// 打开文件/目录，遍历目录内容，打开关闭文件，读写文件，创建删除文件

typedef struct dir_entry {
    char        name[11];  // 8.3 format
    uint8_t     attr;
    uint8_t     ntres;
    uint8_t     create_time_tenth;
    uint16_t    create_time;
    uint16_t    create_date;
    uint16_t    last_access_date;
    uint16_t    first_cluster_hi;
    uint16_t    write_time;
    uint16_t    write_date;
    uint16_t    first_cluster_lo;
    uint32_t    file_size;
} PACKED dir_entry_t;

#define ATTR_READ_ONLY   0x01
#define ATTR_HIDDEN 0x02
#define ATTR_SYSTEM 0x04
#define ATTR_VOLUME_ID 0x08
#define ATTR_DIRECTORY 0x10
#define ATTR_ARCHIVE  0x20
#define ATTR_LONG_NAME ATTR_READ_ONLY|ATTR_HIDDEN|ATTR_SYSTEM|ATTR_VOLUME_ID

// 遍历根目录，列出文件
void fat32_ls_root(fs_fat_t *fs) {
    ASSERT(32 == fs->fat_type);
    ASSERT(0 == fs->root_ents);

    uint32_t *fat = fs->fat32;

    int entries_per_clus = fs->clus_size * fs->sec_size / sizeof(dir_entry_t);
    console_printf("max %d entries in one cluster\n", entries_per_clus);

    // 根目录也是一个文件，占据多个 cluster，逐个簇读取
    uint32_t cls = fs->root_clus;
    // while (cls < (uint32_t)fs->clus_cnt) {
    while (cls < 0x0FFFFFF8) {
        console_printf("reading root dir cluster %u\n", cls);

        // cluster 转换成扇区号（FAT簇编号从2开始）
        int sec = fs->rsvd_secs + fs->fat_num * fs->fat_secs + fs->root_secs;
        sec += (cls - 2) * fs->clus_size;
        cls = fat[cls]; // 下一个簇

        // 读取这个簇并解析
        dir_entry_t *entries = (dir_entry_t*)(fs->rng_clus.vaddr);
        block_read(fs->blk, entries, sec, fs->clus_size);

        // 根目录比较特殊，可能包含一个 entry，只设置属性为ATTR_VOLUME_ID
        // 对于常规目录，这个元素会被认作结束标记
        for (int i = 0; i < entries_per_clus; ++i) {
            console_printf("#%d. root entry '%.11s', attr %02x\n",
                i, entries[i].name, entries[i].attr);
            if (ATTR_VOLUME_ID & entries[i].attr) {
                console_printf("volume ID '%.11s'\n", entries[i].name);
                continue;
            }
            // if (0 == (uint8_t)entries[i].name[0]) {
            //     console_printf("root dir finished at entry %d\n", i);
            //     return; // 到了结尾
            // }
            if (0xe5 == (uint8_t)entries[i].name[0]) {
                console_printf("root entry invalid\n");
                continue;
            }
        }
    }
    // console_printf(" (!%u>%u)\n", cls, fs->clus_cnt);

    // 将根目录内容读取出来
}

//------------------------------------------------------------------------------
// 测试命令
//------------------------------------------------------------------------------

static void chkfat(int argc, char *argv[]) {
    if (argc < 2) {
        console_printf("missing block device name\n");
        return;
    }

    block_dev_t *dev = find_block_by_name(argv[1]);
    if (NULL == dev) {
        console_printf("error: cannot find block device %s\n", argv[1]);
        return;
    }

    fs_fat_t fs;
    fs.blk = dev;

    char sec0[512];
    block_read(dev, sec0, 0, 1);
    fat_determine(&fs, sec0);

    if (fs.fat_type != 32) {
        console_printf("this is not fat32\n");
        return;
    }

    fat32_ls_root(&fs);
}

KSHELL_CMD("fat", chkfat);
