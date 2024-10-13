#ifndef SCHED_LIST_H
#define SCHED_LIST_H

#include "dllist.h"

// 有序链表，按优先级数字从小到大排序，用于就绪队列和阻塞队列
// 提供多种实现，可以选择其中一种
// 链表不能为空，至少 idle 元素必须位于链表中

#define PRIORITY_NUM 32
// #define PRIORITY_IDLE 31

// 每个优先级都有一个独立链表
typedef struct sched_list_arr {
    uint32_t    priorities;
    dlnode_t   *heads[PRIORITY_NUM];
} sched_list_arr_t;

void      sched_list_arr_init(sched_list_arr_t *l);
dlnode_t *sched_list_arr_head(sched_list_arr_t *l);
dlnode_t *sched_list_arr_rotate(sched_list_arr_t *l, dlnode_t *dl);
void      sched_list_arr_insert(sched_list_arr_t *l, int pri, dlnode_t *dl);
void      sched_list_arr_remove(sched_list_arr_t *l, int pri, dlnode_t *dl);
int       sched_list_arr_contains(sched_list_arr_t *l, int pri, dlnode_t *dl);


// 跳表实现
typedef struct sched_list_jmp {
    uint32_t    priorities;
    // dlnode_t   *idle;
    dlnode_t    head;
    dlnode_t   *tails[PRIORITY_NUM];    // 每个优先级的最后一个节点
} sched_list_jmp_t;

void      sched_list_jmp_init(sched_list_jmp_t *l);
dlnode_t *sched_list_jmp_head(sched_list_jmp_t *l);
dlnode_t *sched_list_jmp_rotate(sched_list_jmp_t *l, dlnode_t *dl);
void      sched_list_jmp_insert(sched_list_jmp_t *l, int pri, dlnode_t *dl);
void      sched_list_jmp_remove(sched_list_jmp_t *l, int pri, dlnode_t *dl);
int       sched_list_jmp_contains(sched_list_jmp_t *l, int pri, dlnode_t *dl);


// 默认使用跳表
#define sched_list_t        sched_list_jmp_t
#define sched_list_init     sched_list_jmp_init
#define sched_list_head     sched_list_jmp_head
#define sched_list_rotate   sched_list_jmp_rotate
#define sched_list_insert   sched_list_jmp_insert
#define sched_list_remove   sched_list_jmp_remove
#define sched_list_contains sched_list_jmp_contains

#endif // SCHED_LIST_H
