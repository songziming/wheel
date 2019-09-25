#ifndef DRVS_BLK_H
#define DRVS_BLK_H

#include <base.h>
#include <misc/kref.h>
#include <misc/list.h>

typedef struct blk_ops blk_ops_t;
typedef struct blk_dev blk_dev_t;
typedef struct fs      fs_t;

typedef usize (* blk_read_t)  (blk_dev_t * blk, usize sec, usize n,       u8 * buf);
typedef usize (* blk_write_t) (blk_dev_t * blk, usize sec, usize n, const u8 * buf);

struct blk_ops {
    blk_read_t  read;
    blk_write_t write;
};

struct blk_dev {
    kref_t   ref;
    dlnode_t dl;
    fs_t *   fs;        // must be NULL if vols not empty
    dllist_t vol_list;  // must be empty if fs != NULL
    dlnode_t vol_node;  // node within vol_list
    usize    ops_mode;
    usize    sec_size;
    usize    sec_count;
};

// we save operations and open mode in one field
#define BLK_OPS(d)  ((blk_ops_t *) ((d)->ops_mode & ~3))
#define BLK_MODE(d) ((int)         ((d)->ops_mode &  3))

// operation modes
#define BLK_READ    1
#define BLK_WRITE   2


extern void blk_dev_init  (blk_dev_t * dev, void * del, const blk_ops_t * ops, int mode);
extern void blk_vol_regist(blk_dev_t * dev);
extern void blk_dev_regist(blk_dev_t * dev);
extern int  blk_count_get();
extern blk_dev_t * blk_dev_get(int idx);
extern usize blk_read (blk_dev_t * dev, usize sec, usize n, void * buf);
extern usize blk_write(blk_dev_t * dev, usize sec, usize n, const void * buf);

#endif // DRVS_BLK_H
