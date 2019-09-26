#ifndef DRVS_PART_H
#define DRVS_PART_H

#include <base.h>

typedef struct blk_dev blk_dev_t;

extern int partprobe(blk_dev_t * blk);

#endif // DRVS_PART_H
