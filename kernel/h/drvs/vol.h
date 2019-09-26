#ifndef DRVS_VOL_H
#define DRVS_VOL_H

#include <base.h>
#include <misc/kref.h>
#include <misc/list.h>

typedef struct blk_dev blk_dev_t;
typedef struct fs      fs_t;

typedef struct volume {
    kref_t      ref;
    dlnode_t    dl;             // note in vol_list
    blk_dev_t * blk;
    fs_t      * fs;             // must be NULL if vols not empty
    usize       sec_start;
    usize       sec_count;
} volume_t;

extern volume_t * volume_create(blk_dev_t * blk, usize sec_start, usize sec_count);
extern usize volume_read(volume_t * vol, usize sec, usize n, u8 * buf);
extern usize volume_write(volume_t * vol, usize sec, usize n, const u8 * buf);

extern int  volume_count();
extern void volume_regist(volume_t * vol);
extern volume_t * volume_get(int idx);

#endif // DRVS_VOL_H
