#ifndef POOL_H
#define POOL_H

#include <wheel.h>
#include <spin.h>
#include <page.h>
#include <vmspace.h>

// SLUB 内核对象池
// 每个池管理一种固定大小、固定对齐的对象
// 对象在 slab 内部通过嵌入式的 freelist 管理，零碎片化

typedef struct pool_slab {
    dlnode_t    dl;         // 链接到 pool 的 partial 或 full 链表
    vmrange_t   vm;         // 虚拟地址范围（纳入 g_kernel_vm 管理）
    void       *freelist;   // 第一个空闲对象
    uint32_t    capacity;   // 本 slab 中对象总数
    uint32_t    inuse;      // 当前已分配对象数
} pool_slab_t;

typedef struct pool {
    spin_t      lock;
    dlnode_t    partial;    // 部分满 slab 的循环链表头
    dlnode_t    full;       // 全满 slab 的循环链表头
    pglist_t    pages;      // 所有 slab 的物理页块
    const char *name;
    size_t      obj_size;   // 对象大小（已对齐到 obj_align）
    uint16_t    objs_per_slab;
    uint16_t    slab_order; // slab 的页分配 rank
    uint64_t    allocs;     // 分配次数
    uint64_t    frees;      // 释放次数
} pool_t;

// 初始化对象池。obj_size 会自动对齐到 align 的倍数
void pool_init(pool_t *pool, const char *name, size_t obj_size, size_t align);

// 分配一个对象，内存不足时 panic
MALLOC void *pool_alloc(pool_t *pool);

// 释放对象回池
void pool_free(pool_t *pool, void *ptr);

// 销毁整个池，归还所有内存
void pool_destroy(pool_t *pool);

#endif // POOL_H
