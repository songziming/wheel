#include "block.h"
#include <vmspace.h>
#include <heap.h>

#include <kstring.h>
#include <format.h>
#include <debug.h>
#include <console.h>
#include <kshell.h>

// 对外提供统一的 volumn 接口
// 支持 FAT16/FAT32 两种文件系统
// 不追求性能，够用即可

//------------------------------------------------------------------------------
// FAT 引导扇区结构
//------------------------------------------------------------------------------

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

// FAT16/FAT32 引导扇区格式
typedef struct fatbs {
    uint8_t jmp_boot[3];
    uint8_t oem_name[8];
    bpb_t   bpb;
    union {
        struct {
            ebpb_t  ebpb;
            ebr_t   ebr32;
        };
        ebr_t   ebr16;
    };
} PACKED fatbs_t;

//------------------------------------------------------------------------------
// FAT 挂载的卷
//------------------------------------------------------------------------------

typedef struct fat32_volumn {
    block_dev_t *blk;
    uint32_t     data_start; // 数据区的起始扇区号
    uint32_t     cluster_shift; // 簇到扇区的移位
    uint32_t     cluster_count; // 簇个数
    uint32_t     root_cluster;  // 根目录的第一个 cluster 编号
    vmrange_t    fat_cache;     // FAT 单链表的缓存
    vmrange_t    cluster_cache; // 读写簇使用的缓存
    uint32_t    *fat;
    uint8_t     *data;
} fat32_volumn_t;

// 作用类似 inode，可以快速定位文件数据的位置，并非表示打开的文件
typedef struct fs_entry {
    const char *name;
    uint32_t    first_cluster;
    uint32_t    size;
    int         is_dir : 1;
    int         hidden : 1;
} fs_entry_t;

// 代表一个打开的文件或目录
typedef struct fat32_handle {
    fs_entry_t  ent;
    vmrange_t   cluster_cache;  // 数据读取到哪里
    uint32_t    curr_clus;      // 当前读到哪个簇
    uint32_t    curr_pos;       // 当前读到簇里面哪个字节
} fat32_handle_t;

// 遍历文件夹的回调，返回非零表示是否不再继续遍历
// 传给回调的参数会被销毁，回调需要自己复制一份
typedef int (*fs_walker_t)(const fs_entry_t *item, void *user);

//------------------------------------------------------------------------------
// 检查块设备是不是 FAT32 文件系统
//------------------------------------------------------------------------------

// caller 通常已经读取了第一个扇区
fat32_volumn_t *fat32_mount(block_dev_t *blk, const uint8_t *sec0) {
    if ((0x55 != sec0[510]) || (0xaa != sec0[511])) {
        logk("warning: boot signature invalid!\n");
    }

    const fatbs_t *bs = (const fatbs_t*)sec0;

    // FS 可能没有用满整个块设备
    uint32_t total_secs = bs->bpb.tot_sec_16;
    if (0 == total_secs) {
        total_secs = bs->bpb.tot_sec_32;
    }
    if (total_secs > blk->sec_num) {
        logk("warning: FS corrupted, sector count %u larger than %zu\n",
            total_secs, blk->sec_num);
            return NULL;
    }

    uint32_t sec_size  = bs->bpb.byts_per_sec;
    if (sec_size != blk->sec_size) {
        logk("warning: FS sector size %u different from block sector %u\n",
            sec_size, blk->sec_size);
            return NULL;
    }

    // cluster 大小，必然是 2 的幂，必然小于 32K，也可能小于一个页
    size_t clus_size = bs->bpb.sec_per_clus * sec_size;
    int clus_rank = 0;
    if (clus_size > PAGE_SIZE) {
        clus_rank = __builtin_ctz(clus_size >> PAGE_SHIFT);
    }

    // 计算根目录区占据的扇区数（FAT32 为 0）
    uint32_t root_ents = bs->bpb.root_ent_cnt;
    uint32_t root_secs = (root_ents * 32 + sec_size - 1) / sec_size;

    // 计算一个 FAT 占据的扇区数
    uint32_t fat_secs = bs->bpb.fat_sz_16;
    if (0 == fat_secs) {
        fat_secs = bs->ebpb.fat_sz_32;
    }

    // 计算数据区的簇个数（向下取整）
    uint32_t data_start = bs->bpb.rsvd_sec_cnt + fat_secs * bs->bpb.num_fats + root_secs;
    uint32_t cluster_cnt = (total_secs - data_start) / bs->bpb.sec_per_clus;

    // 根据簇数量判断文件系统类型
    if (cluster_cnt < 4085) {
        logk("error: this is FAT12\n");
        return NULL;
    } else if (cluster_cnt < 65525) {
        logk("error: this is FAT16\n");
        return NULL;
    } else {
        logk("correct, this is FAT32\n");
    }

    fat32_volumn_t *vol = (fat32_volumn_t*)kernel_heap_alloc(sizeof(fat32_volumn_t));
    if (NULL == vol) {
        logk("cannot allocate volumn object!\n");
        return NULL;
    }

    // 为 FAT 分配空间
    // TODO 目前只能按 2 的幂次分配内存，可能造成浪费
    // TODO FAT 有可能超过 block 最大可分配大小
    size_t fat_pages = (fat_secs * sec_size + PAGE_SIZE - 1) >> PAGE_SHIFT;
    if (fat_pages & (fat_pages - 1)) {
        logk("warning: FAT requires %zd pages, rounding up\n", fat_pages);
        fat_pages <<= 1;
        while (fat_pages & (fat_pages - 1)) {
            fat_pages &= fat_pages - 1;
        }
    }
    int fat_rank = __builtin_ctz(fat_pages);
    vol->fat = (uint32_t*)vmspace_alloc(&g_kernel_vm, &vol->fat_cache,
        POOL_ZONE_START, POOL_ZONE_END, fat_rank, PT_FS, MMU_WRITE);
    if (NULL == vol->fat) {
        logk("cannot allocate space for FAT table!\n");
        kernel_heap_free(vol);
        return NULL;
    }
    vol->fat_cache.desc = "FAT32-fat-cache";

    // 为读写用的簇缓存分配空间
    vol->data = (uint8_t*)vmspace_alloc(&g_kernel_vm, &vol->cluster_cache,
        POOL_ZONE_START, POOL_ZONE_END, clus_rank, PT_FS, MMU_WRITE);
    vol->cluster_cache.desc = "FAT32-cluster-cache";
    if (NULL == vol->data) {
        logk("cannot allocate space for FAT32 cluster cache!\n");
        vmspace_remove(&g_kernel_vm, &vol->fat_cache);
        kernel_heap_free(vol);
        return NULL;
    }

    // 读取 FAT
    block_read(blk, vol->fat, bs->bpb.rsvd_sec_cnt, fat_secs);

    vol->blk = blk;
    vol->cluster_shift = __builtin_ctz(bs->bpb.sec_per_clus);
    vol->cluster_count = cluster_cnt;
    vol->root_cluster = bs->ebpb.root_clus;
    vol->data_start = data_start;
    return vol;
}

//------------------------------------------------------------------------------
// 遍历根目录内容
//------------------------------------------------------------------------------

typedef struct fat_entry {
    char        name[11];  // 8.3 format
    uint8_t     attr;
    uint8_t     ntres;
    uint8_t     create_time_tenth;  // 14
    uint16_t    create_time;        // 16
    uint16_t    create_date;
    uint16_t    last_access_date;
    uint16_t    first_cluster_hi;
    uint16_t    write_time;
    uint16_t    write_date;
    uint16_t    first_cluster_lo;   // 28
    uint32_t    file_size;
} PACKED fat_entry_t;

typedef struct fat_long_entry {
    uint8_t     order;
    uint8_t     name1[10];
    uint8_t     attr;
    uint8_t     type;
    uint8_t     checksum;
    uint8_t     name2[12];
    uint16_t    first_cluster_lo;
    uint8_t     name3[4];
} PACKED fat_long_entry_t;

#define ATTR_READ_ONLY  0x01
#define ATTR_HIDDEN     0x02
#define ATTR_SYSTEM     0x04
#define ATTR_VOLUME_ID  0x08
#define ATTR_DIRECTORY  0x10
#define ATTR_ARCHIVE    0x20
#define ATTR_LONG_NAME (ATTR_READ_ONLY|ATTR_HIDDEN|ATTR_SYSTEM|ATTR_VOLUME_ID)

// 遍历一个目录
// 如果提前退出，则回调的返回值就是本函数的返回值，否则返回零
// 传给回调的 fs_entry 是局部变量，fs_entry 里面的字符串也是局部变量
static int fat32_walk_dir(fat32_volumn_t *vol, uint32_t clus, fs_walker_t cb, void *user) {
    fat_entry_t *entries = (fat_entry_t*)vol->data;
    size_t ent_per_clus = (vol->blk->sec_size << vol->cluster_shift) / sizeof(fat_entry_t);

    char short_name[13]; // 8.3 再加上分隔符、终止符
    char long_name[256]; // 长文件名用的是宽字符
    int long_order = 0; // 前一个长名条目的编号
    fs_entry_t ent;

    while ((clus >= 2) && (clus < vol->cluster_count)) {
        int sec = vol->data_start + ((clus - 2) << vol->cluster_shift);
        block_read(vol->blk, entries, sec, 1U << vol->cluster_shift);
        clus = vol->fat[clus]; // 通过 FAT 找到下一个簇

        for (size_t i = 0; i < ent_per_clus; ++i) {
            if (0xe5 == (uint8_t)entries[i].name[0]) {
                continue; // 无效条目
            }
            if (0 == (uint8_t)entries[i].name[0]) {
                return 0; // 到了结尾
            }
            if (ATTR_LONG_NAME == (entries[i].attr & ATTR_LONG_NAME)) {
                // 长文件名条目里面只记录文件名，若干长文件名后面是一个短文件名条目
                fat_long_entry_t *lent = (fat_long_entry_t*)(entries + i);
                // if (0x40 & lent->order) {
                //     logk("start of long name entries\n");
                // } else if (long_order - 1 != lent->order) {
                //     logk("long name entries corrupt!");
                // }
                long_order = lent->order & ~0x40;

                // 一个 long-name-entry 包含 13 个宽字符
                // 我们将这 13 个宽字符转换为 ascii 再拼接
                uint16_t wname[13];
                kmemcpy(wname, lent->name1, sizeof(lent->name1));
                kmemcpy(wname + 5, lent->name2, sizeof(lent->name2));
                kmemcpy(wname + 11, lent->name3, sizeof(lent->name3));
                char *name = &long_name[(long_order - 1) * 13];
                for (int j = 0; j < 13; ++j) {
                    if (wname[j] <= 0x7f) {
                        name[j] = (char)wname[j];
                    } else {
                        name[j] = '?';
                    }
                }
                // logk("appending long name (%d) --> %.13s\n", long_order, name);
                continue;
            }

            // 特殊文件直接跳过

            // 记录文件名
            if (long_order == 1) {
                long_order = 0;
                ent.name = long_name; // 长文件名自带终止符
            } else {
                // 空格改成终止符
                char *raw = entries[i].name;
                // logk("old 8.3 name (%.11s)\n", raw);
                for (int j = 0; j < 11; ++j) {
                    if (' ' == raw[j]) {
                        raw[j] = '\0';
                    }
                }
                snprintk(short_name, sizeof(short_name), "%s.%.3s", raw, raw + 8);
                ent.name = short_name;
            }

            // 记录属性
            ent.is_dir = (entries[i].attr & ATTR_DIRECTORY) ? 1 : 0;
            ent.hidden = (entries[i].attr & ATTR_HIDDEN) ? 1 : 0;
            ent.first_cluster   = entries[i].first_cluster_hi;
            ent.first_cluster <<= 16;
            ent.first_cluster  |= entries[i].first_cluster_lo;
            ent.size = entries[i].file_size;

            // 调用回调函数，返回 0 表示不再
            int ret = cb(&ent, user);
            if (ret) {
                return ret;
            }
        }
    }

    return 0;
}

//------------------------------------------------------------------------------
// 遍历打开的目录，访问下一个 fs_entry
//------------------------------------------------------------------------------

// 如果已经是最后一个 entry，则返回 1
int next_dir_entry(fat32_volumn_t *vol, fat32_handle_t *h, fs_entry_t *next) {
    size_t cluster_size = vol->blk->sec_size << vol->cluster_shift;
    size_t ent_per_clus = cluster_size / sizeof(fat_entry_t);

    fat_entry_t *entry = (fat_entry_t*)(h->cluster_cache.vaddr + h->curr_pos);
    if (0 == entry->name[0]) {
        return 1; // 到了结尾
    }

    // 不是终止 entry，必然存在下一个 entry
    h->curr_pos += sizeof(fat_entry_t);
    if (h->curr_pos >= cluster_size) {
        h->curr_clus = vol->fat[h->curr_clus];
    }

    // 从当前 entry 解析文件名
    // 如果遇到长文件名 entry，则需要多次循环

    return 0;
}

//------------------------------------------------------------------------------

static int fat32_ls_cb(const fs_entry_t *ent, void *user UNUSED) {
    if (ent->is_dir) {
        console_printf("%s/\n", ent->name);
    } else {
        console_printf("%s\n", ent->name);
    }
    return 0;
}

// 如果 d==NULL，表示查看根目录内容
void fat32_ls(fat32_volumn_t *vol, const fs_entry_t *d) {
    uint32_t clus = d ? d->first_cluster : vol->root_cluster;
    fat32_walk_dir(vol, clus, fat32_ls_cb, NULL);
}

static int fat32_find_cb(const fs_entry_t *ent, void *user) {
    fs_entry_t *dst = (fs_entry_t*)user;
    if (0 == kstrcmp(ent->name, dst->name)) {
        // ent 是局部变量，回调结束后就被销毁，里面的字符串也是局部的，只在本函数里有效
        // 所以，name 应该保留 dst 里面的取值，这是 caller 传入的
        const char *oldstr = dst->name;
        kmemcpy(dst, ent, sizeof(fs_entry_t));
        dst->name = oldstr;
        return 1;
    }
    return 0;
}

// 寻找目录 d 内部，名字是 name 的元素
// 找到则返回 1，未找到返回 0
// 通过 child 返回的信息包括了 name 字符串，这个字符串指向局部数据，返回之后被销毁
int fat32_find(fat32_volumn_t *vol, const fs_entry_t *d, const char *name, fs_entry_t *child) {
    child->name = name;
    uint32_t clus = d ? d->first_cluster : vol->root_cluster;
    return fat32_walk_dir(vol, clus, fat32_find_cb, child);
}

//------------------------------------------------------------------------------
// 调试命令
//------------------------------------------------------------------------------

static fat32_volumn_t *g_vol = NULL;

static void fatmount(int argc, char *argv[]) {
    if (NULL != g_vol) {
        console_printf("already mounted\n");
        return;
    }
    if (argc < 2) {
        console_printf("missing block device name\n");
        return;
    }

    block_dev_t *dev = find_block_by_name(argv[1]);
    if (NULL == dev) {
        console_printf("error: cannot find block device %s\n", argv[1]);
        return;
    }
    if (512 != dev->sec_size) {
        console_printf("error: %s sector size is %u\n", argv[1], dev->sec_size);
        return;
    }

    uint8_t sec0[512];
    block_read(dev, sec0, 0, 1);
    g_vol = fat32_mount(dev, sec0);
}

// 沿着路径找到最后一级目录，显示末级目录的内容
static void fatls(int argc, char *argv[]) {
    if (NULL == g_vol) {
        console_printf("not mounted\n");
        return;
    }

    fs_entry_t next;
    fs_entry_t *curr = NULL;

    for (int i = 1; i < argc; ++i) {
        if (0 == fat32_find(g_vol, curr, argv[i], &next)) {
            console_printf("cannot find %s\n", argv[i]);
            return;
        }
        if (0 == next.is_dir) {
            console_printf("error: `%s` is not directory!\n", next.name);
            return;
        }
        curr = &next;
    }

    console_printf("listing content under (%s):\n",
        curr ? curr->name : "root");
    fat32_ls(g_vol, curr);
}

// 沿着路径找到最后一层目录或文件
static void fatfile(int argc, char *argv[]) {
    if (NULL == g_vol) {
        console_printf("not mounted\n");
        return;
    }
    if (argc < 2) {
        console_printf("error: no path given\n");
        return;
    }

    fs_entry_t *curr = NULL;
    fs_entry_t next;
    for (int i = 1; i < argc; ++i) {
        if ((NULL != curr) && (0 == curr->is_dir)) {
            console_printf("error: `%s` is not directory!\n", curr->name);
            return;
        }
        if (0 == fat32_find(g_vol, curr, argv[i], &next)) {
            console_printf("cannot find %s\n", argv[i]);
            return;
        }
        curr = &next;
    }

    if (curr->is_dir) {
        console_printf("this is directory!\n");
    }
    console_printf("file entry `%s`\n", curr->name);
    console_printf("cluster at %u\n", curr->first_cluster);
    console_printf("file size %u\n", curr->size);
}


KSHELL_CMD("fatmount", fatmount);
KSHELL_CMD("fatls", fatls);
KSHELL_CMD("fatfile", fatfile);
