#include <gtest/gtest.h>

extern "C" {
#include "sched_list.h"
}


typedef struct item {
    dlnode_t dl;
    int      priority;
} item_t;

// static item_t g_idle = { .priority = PRIORITY_IDLE };


static sched_list_arr_t g_arr;
#define LIST_NAME        SchedListArr
#define LIST_INIT()      sched_list_arr_init(&g_arr)
#define LIST_HEAD()      containerof(sched_list_arr_head(&g_arr), item_t, dl)
#define LIST_INSERT(x)   sched_list_arr_insert(&g_arr, x.priority, &x.dl)
#define LIST_REMOVE(x)   sched_list_arr_remove(&g_arr, x.priority, &x.dl)
#define LIST_CONTAINS(x) sched_list_arr_contains(&g_arr, x.priority, &x.dl)
#include "sched_list.test.h"
#undef LIST_NAME
#undef LIST_INIT
#undef LIST_HEAD
#undef LIST_INSERT
#undef LIST_REMOVE
#undef LIST_CONTAINS


static sched_list_jmp_t g_jmp;
#define LIST_NAME        SchedListJmp
#define LIST_INIT()      sched_list_jmp_init(&g_jmp)
#define LIST_HEAD()      containerof(sched_list_jmp_head(&g_jmp), item_t, dl)
#define LIST_INSERT(x)   sched_list_jmp_insert(&g_jmp, x.priority, &x.dl)
#define LIST_REMOVE(x)   sched_list_jmp_remove(&g_jmp, x.priority, &x.dl)
#define LIST_CONTAINS(x) sched_list_jmp_contains(&g_jmp, x.priority, &x.dl)
#include "sched_list.test.h"
#undef LIST_NAME
#undef LIST_INIT
#undef LIST_HEAD
#undef LIST_INSERT
#undef LIST_REMOVE
#undef LIST_CONTAINS
