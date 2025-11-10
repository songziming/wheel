#include <test.h>
#include <stdlib.h>

#include "../sources/heap.c"

static uint8_t g_test_buff[2048];
static mem_heap_t g_test_heap;

static void setup() {
    heap_init(&g_test_heap, g_test_buff, sizeof(g_test_buff));
}

static void teardown() {
    //
}

// 检查堆结构是否完好
static void check_heap_integrity() {
    chunk_hdr_t *hdr = (chunk_hdr_t *)g_test_buff;
    uint8_t *end = g_test_buff + sizeof(g_test_buff);

    uint32_t prevsize = 0; // 前一个 chunk 的大小
    uint32_t previnuse = 0; // 前一个 chunk 是否已分配
    for (int i = 0; (uint8_t *)hdr < end; ++i) {
        EXPECT_TRUE(prevsize == hdr->prevsize);
        EXPECT_TRUE(previnuse | (hdr->selfsize & CHUNK_INUSE)); // 不能有两个相邻的 free chunk

        prevsize = hdr->selfsize & ~CHUNK_INUSE;
        previnuse = hdr->selfsize & CHUNK_INUSE;
        hdr = (chunk_hdr_t *)((size_t)hdr + prevsize);
    }
}

TEST_F(Heap, Setup, setup, teardown) {
    check_heap_integrity();

    char *p1 = heap_alloc(&g_test_heap, 0x10);  *p1 = 1;
    char *p2 = heap_alloc(&g_test_heap, 0x20);  *p2 = 2;
    char *p3 = heap_alloc(&g_test_heap, 0x30);  *p3 = 3;
    char *p4 = heap_alloc(&g_test_heap, 0x40);  *p4 = 4;
    char *p5 = heap_alloc(&g_test_heap, 0x50);  *p5 = 5;
    check_heap_integrity();

    heap_free(&g_test_heap, p2);
    check_heap_integrity();
    heap_free(&g_test_heap, p4);
    check_heap_integrity();
    heap_free(&g_test_heap, p3);
    check_heap_integrity();

    char *p6 = heap_alloc(&g_test_heap, 0x60);  *p6 = 6;
    char *p7 = heap_alloc(&g_test_heap, 0x70);  *p7 = 7;
    check_heap_integrity();
}
