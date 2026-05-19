#ifndef FENCE_H
#define FENCE_H

#include <spin.h>
#include <dllist.h>

typedef struct fence {
    spin_t lock;
    dlnode_t penders;
} fence_t;

void fence_init(fence_t *fence);
void fence_wait(fence_t *fence);
void fence_signal(fence_t *fence);

#endif // FENCE_H
