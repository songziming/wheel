// 调度用的链表
// 链表中元素按优先级从小到大排序，放在一个链表中
// 另外使用跳表快速定位特定优先级的最后一个元素

#include "sched_list.h"

#include <library/debug.h>
#include <library/string.h>


//------------------------------------------------------------------------------
// 多链表实现
//------------------------------------------------------------------------------


void sched_list_arr_init(sched_list_arr_t *l) {
    ASSERT(NULL != l);

    memset(l, 0, sizeof(sched_list_arr_t));
}

dlnode_t *sched_list_arr_head(sched_list_arr_t *l) {
    ASSERT(NULL != l);

    if (0 == l->priorities) {
        return NULL;
    }

    int top = __builtin_ctz(l->priorities);
    ASSERT(NULL != l->heads[top]);
    return l->heads[top];
}

void sched_list_arr_insert(sched_list_arr_t *l, int pri, dlnode_t *dl) {
    ASSERT(NULL != l);
    ASSERT(NULL != dl);
    ASSERT(pri >= 0);
    ASSERT(pri < PRIORITY_NUM);

    if ((1U << pri) & l->priorities) {
        dl_insert_before(dl, l->heads[pri]);
    } else {
        l->heads[pri] = dl_init_circular(dl);
        l->priorities |= 1U << pri;
    }
}

void sched_list_arr_remove(sched_list_arr_t *l, int pri, dlnode_t *dl) {
    ASSERT(NULL != l);
    ASSERT(NULL != dl);
    ASSERT(pri >= 0);
    ASSERT(pri < PRIORITY_NUM);
    ASSERT((l->heads[pri] == dl) || dl_contains(l->heads[pri], dl));

    if (dl_is_lastone(dl)) {
        ASSERT(l->heads[pri] == dl);
        l->heads[pri] = NULL;
        l->priorities &= ~(1U << pri);
        return;
    }

    dlnode_t *next = dl_remove(dl)->next;
    if (l->heads[pri] == dl) {
        l->heads[pri] = next;
    }
}

int sched_list_arr_contains(sched_list_arr_t *l, int pri, dlnode_t *dl) {
    ASSERT(NULL != l);
    ASSERT(NULL != dl);
    ASSERT(pri >= 0);
    ASSERT(pri < PRIORITY_NUM);

    if ((1U << pri) & l->priorities) {
        dlnode_t *head = l->heads[pri];
        return (head == dl) || dl_contains(head, dl);
    }
    return 0;
}


//------------------------------------------------------------------------------
// 跳表实现的有序链表
//------------------------------------------------------------------------------

void sched_list_jmp_init(sched_list_jmp_t *l) {
    ASSERT(NULL != l);

    memset(l, 0, sizeof(sched_list_jmp_t));
    dl_init_circular(&l->head);
}

dlnode_t *sched_list_jmp_head(sched_list_jmp_t *l) {
    return l->priorities ? l->head.next : NULL;
}

// 返回优先级 [0..pri] 之间最后一个节点，如果没有就返回 head
// 如果传入 pri = -1，则直接返回 head
static inline dlnode_t *last(sched_list_jmp_t *l, int pri) {
    ASSERT(pri >= -1);

    // 取优先级掩码中 [0..pri] 部分
    // 中间变量可能超过 32-bit，需要使用 uint64
    uint32_t mask = l->priorities & ((1UL << (pri + 1)) - 1);
    if (0 == mask) {
        return &l->head;
    } else {
        return l->tails[31 - __builtin_clz(mask)];
    }
}

void sched_list_jmp_insert(sched_list_jmp_t *l, int pri, dlnode_t *dl) {
    ASSERT(NULL != l);
    ASSERT(NULL != dl);
    ASSERT(pri >= 0);
    ASSERT(pri < PRIORITY_NUM);

    dlnode_t *prev = last(l, pri);
    dl_insert_after(dl, prev);
    l->tails[pri] = dl;
    l->priorities |= 1U << pri;
}

void sched_list_jmp_remove(sched_list_jmp_t *l, int pri, dlnode_t *dl) {
    ASSERT(NULL != l);
    ASSERT(NULL != dl);
    ASSERT(pri >= 0);
    ASSERT(pri < PRIORITY_NUM);
    ASSERT(l->priorities & (1U << pri));
    ASSERT(dl_contains(&l->head, dl));

    dlnode_t *prev = dl_remove(dl)->prev;
    if ((dl == l->tails[pri]) && (last(l, pri-1) == prev)) {
        l->priorities &= ~(1U << pri);
        l->tails[pri] = NULL;
    }
}

int sched_list_jmp_contains(sched_list_jmp_t *l, int pri, dlnode_t *dl) {
    (void)pri;
    return dl_contains(&l->head, dl);
}






//------------------------------------------------------------------------------

// 链表共有32个优先级，也有32个哨兵节点
// 相邻两个哨兵节点之间的范围，就是该优先级元素构成的子链表

// heads[x]..heads[x+1] 之间的子序列就是优先级为 x 的元素

typedef struct chain_list {
    dlnode_t heads[32];
} chain_list_t;

// void chain_list_init(chain_list_t *list) {
//     dl_init_circular(&list->heads[0]);
//     for (int i = 1; i < 32; ++i) {
//         dl_insert_before(&list->heads[i], &list->heads[0]);
//     }
// }

// void chain_list_insert(chain_list_t *list, int pri, dlnode_t *dl) {

// }

