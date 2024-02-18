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
}
