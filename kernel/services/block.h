#ifndef BLOCK_H
#define BLOCK_H

// 块设备通用接口

#include <dllist.h>

typedef struct block_ops block_ops_t;
typedef struct block_dev block_dev_t;

struct block_ops {
    void (*show)(block_dev_t *dev);
    void (*read)(block_dev_t *dev, void *dst, uint64_t sec, uint64_t nsecs);
    void (*write)(block_dev_t *dev, const void *src, uint64_t sec, uint64_t nsecs);
};

struct block_dev {
    dlnode_t dl;
    const block_ops_t *ops;
    const char *name;
    uint32_t sec_size;
    uint64_t sec_num;
};

// INIT_TEXT void block_dev_init();
void add_block_dev(block_dev_t *dev);
block_dev_t *find_block_by_name(const char *name);

void block_read(block_dev_t *dev, void *dst, uint64_t sec, uint64_t nsecs);
void block_write(block_dev_t *dev, const void *src, uint64_t sec, uint64_t nsecs);

#endif // BLOCK_H
