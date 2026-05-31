#ifndef FS_FAT_H
#define FS_FAT_H

#include "block.h"

// 通用的文件系统接口，每个FS都是独立树形结构

typedef struct sys_file sys_file_t;
typedef struct sys_fs sys_fs_t;

typedef struct fs_ops {
    void        (*unmount)(sys_fs_t *fs);   // 释放这个FS
    sys_file_t* (*open)(sys_fs_t *fs, const char *path);
} fs_ops_t;

typedef struct file_ops {
    size_t  (*read)(sys_file_t *file, void *dst, size_t size);
    size_t  (*seek)(sys_file_t *file, size_t pos);
    void    (*close)(sys_file_t *file);
} file_ops_t;

// 表示一个打开的文件，被任务持有
struct sys_file {
    file_ops_t *ops;
};

struct sys_fs {
    fs_ops_t *ops;
};

// 创建一个文件系统
sys_fs_t *fat_mount(block_dev_t *dev);

#endif // FS_FAT_H
