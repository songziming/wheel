#include <gtest/gtest.h>
#include "page.mock.h"
#include <early_alloc.mock.h>
#include <string_view>

extern "C" {
    #include <arch_api.h>
    #include <cpu/features.h>
    #include <page.h>
    #include <debug.h>
}

#define K  1024L
#define M (1024L*K)
#define G (1024L*M)

class MmuTest : public ::testing::Test {
protected:
    PageContext pc_{16 * K};

    void SetUp() override {
        g_cpu_features |= CPU_FEATURE_1G | CPU_FEATURE_NX;
    }
};

// 测试各级映射
TEST_F(MmuTest, MapUnmapNormal) {
    size_t pgtbl = mmu_create();
    mmu_attr_t attrs;

    EXPECT_EQ(mmu_translate(pgtbl, 512*G, &attrs), 0);

    uint32_t free_num = page_free_count();

    // map va [4G,10G) to pa [2G,8G)
    mmu_map(pgtbl, 4*G, 10*G, 2*G, MMU_WRITE);
    EXPECT_EQ(mmu_translate(pgtbl, 4*G-4*K, &attrs), 0);
    EXPECT_EQ(mmu_translate(pgtbl, 4*G, &attrs), 2*G);
    EXPECT_EQ(mmu_translate(pgtbl, 10*G-4*K, &attrs), 8*G-4*K);
    EXPECT_EQ(mmu_translate(pgtbl, 10*G, &attrs), 0);
    // unmap va [6G,8G)
    mmu_unmap(pgtbl, 6*G, 8*G);
    EXPECT_EQ(mmu_translate(pgtbl, 6*G-4*K, &attrs), 4*G-4*K);
    EXPECT_EQ(mmu_translate(pgtbl, 6*G, &attrs), 0);
    EXPECT_EQ(mmu_translate(pgtbl, 8*G-4*K, &attrs), 0);
    EXPECT_EQ(mmu_translate(pgtbl, 8*G, &attrs), 6*G);
    // unmap all
    mmu_unmap(pgtbl, 0, 4*512*G);
    EXPECT_EQ(free_num, page_free_count());

    // map va [4M,14M) to pa [2M,12M)
    mmu_map(pgtbl, 4*M, 14*M, 2*M, MMU_WRITE);
    EXPECT_EQ(mmu_translate(pgtbl, 4*M-4*K, &attrs), 0);
    EXPECT_EQ(mmu_translate(pgtbl, 4*M, &attrs), 2*M);
    EXPECT_EQ(mmu_translate(pgtbl, 14*M-4*K, &attrs), 12*M-4*K);
    EXPECT_EQ(mmu_translate(pgtbl, 14*M, &attrs), 0);
    // unmap va [6M, 10M)
    mmu_unmap(pgtbl, 6*M, 10*M);
    EXPECT_EQ(mmu_translate(pgtbl, 6*M-4*K, &attrs), 4*M-4*K);
    EXPECT_EQ(mmu_translate(pgtbl, 6*M, &attrs), 0);
    EXPECT_EQ(mmu_translate(pgtbl, 10*M-4*K, &attrs), 0);
    EXPECT_EQ(mmu_translate(pgtbl, 10*M, &attrs), 8*M);
    // unmap all
    mmu_unmap(pgtbl, 0, 16*M);
    EXPECT_EQ(free_num, page_free_count());

    // map va [4K,44K) to pa [24K,64K)
    mmu_map(pgtbl, 4*K, 44*K, 24*K, MMU_WRITE);
    EXPECT_EQ(mmu_translate(pgtbl, 0, &attrs), 0);
    EXPECT_EQ(mmu_translate(pgtbl, 4*K, &attrs), 24*K);
    EXPECT_EQ(mmu_translate(pgtbl, 40*K, &attrs), 60*K);
    EXPECT_EQ(mmu_translate(pgtbl, 44*K, &attrs), 0);
    // unmap va [16K,28K)
    mmu_unmap(pgtbl, 16*K, 28*K);
    EXPECT_EQ(mmu_translate(pgtbl, 12*K, &attrs), 32*K);
    EXPECT_EQ(mmu_translate(pgtbl, 16*K, &attrs), 0);
    EXPECT_EQ(mmu_translate(pgtbl, 24*K, &attrs), 0);
    EXPECT_EQ(mmu_translate(pgtbl, 28*K, &attrs), 48*K);
    // unmap all
    mmu_unmap(pgtbl, 0, 52*K);
    EXPECT_EQ(free_num, page_free_count());

    mmu_delete(pgtbl);
}


// 建立映射，破环原本的 2M/1G-page，检查原 mapping 能否保留
TEST_F(MmuTest, RemapBreakBig) {
    size_t pgtbl = mmu_create();
    mmu_attr_t attrs;

    // map va [0,8G) to pa [1G,9G)
    mmu_map(pgtbl, 0, 8*G, 1*G, MMU_WRITE);
    EXPECT_EQ(mmu_translate(pgtbl, 0, &attrs), 1*G);
    EXPECT_EQ(mmu_translate(pgtbl, 8*G-4*K, &attrs), 9*G-4*K);

    // map va [2M,8M) to pa [12M,18M)
    mmu_map(pgtbl, 2*M, 8*M, 12*M, MMU_WRITE);
    EXPECT_EQ(mmu_translate(pgtbl, 2*M, &attrs), 12*M);
    EXPECT_EQ(mmu_translate(pgtbl, 8*M-4*K, &attrs), 18*M-4*K);
    // check remaining
    EXPECT_EQ(mmu_translate(pgtbl, 2*M-4*K, &attrs), 1*G+2*M-4*K);
    EXPECT_EQ(mmu_translate(pgtbl, 8*M, &attrs), 1*G+8*M);

    // map va [4M+12K,4M+28K) to pa [32K,48K)
    mmu_map(pgtbl, 4*M+12*K, 4*M+28*K, 32*K, MMU_WRITE);
    EXPECT_EQ(mmu_translate(pgtbl, 4*M+12*K, &attrs), 32*K);
    EXPECT_EQ(mmu_translate(pgtbl, 4*M+24*K, &attrs), 44*K);
    // check old remaining mapping
    EXPECT_EQ(mmu_translate(pgtbl, 4*M+8*K, &attrs), 14*M+8*K);
    EXPECT_EQ(mmu_translate(pgtbl, 4*M+28*K, &attrs), 14*M+28*K);

    // unmap all
    mmu_unmap(pgtbl, 2*M, 6*M); // including 4K-mappings
    mmu_unmap(pgtbl, 0, 8*G); // including 2M-mappings

    mmu_delete(pgtbl);
}


// 建立映射，使用大页覆盖覆盖子表中的小页
TEST_F(MmuTest, RemapOverwriteSub) {
    size_t pgtbl = mmu_create();
    mmu_attr_t attrs;

    // map va [2M+8K,4M+48K) to pa [8K,2M+48K)
    mmu_map(pgtbl, 2*M+8*K, 4*M+48*K, 8*K, MMU_WRITE);
    EXPECT_EQ(mmu_translate(pgtbl, 2*M+8*K, &attrs), 8*K);
    EXPECT_EQ(mmu_translate(pgtbl, 4*M+44*K, &attrs), 2*M+44*K);

    // map va [2M,4M) to pa [12M,14M), replacing 4K-entries
    mmu_map(pgtbl, 2*M, 4*M, 12*M, MMU_WRITE);
    EXPECT_EQ(mmu_translate(pgtbl, 2*M+8*K, &attrs), 12*M+8*K);
    EXPECT_EQ(mmu_translate(pgtbl, 4*M-4*K, &attrs), 14*M-4*K);
    // check remaining
    EXPECT_EQ(mmu_translate(pgtbl, 4*M, &attrs), 2*M);
    EXPECT_EQ(mmu_translate(pgtbl, 4*M+44*K, &attrs), 2*M+44*K);

    // map va [0,2G) to [2G,4G), replacing 2M-entries
    mmu_map(pgtbl, 0, 2*G, 2*G, MMU_WRITE);

    mmu_delete(pgtbl);
}


// 取消映射，破环原本的 2M-page，检查原 mapping 能否保留
TEST_F(MmuTest, UnmapBreak2M) {
    size_t pgtbl = mmu_create();
    mmu_attr_t attrs;

    // map va [2M,8M) to pa [12M,18M)
    mmu_map(pgtbl, 2*M, 8*M, 12*M, MMU_WRITE);
    EXPECT_EQ(mmu_translate(pgtbl, 2*M, &attrs), 12*M);
    EXPECT_EQ(mmu_translate(pgtbl, 8*M-4*K, &attrs), 18*M-4*K);

    // unmap va [4M+12K,4M+28K)
    mmu_unmap(pgtbl, 4*M+12*K, 4*M+28*K);
    EXPECT_EQ(mmu_translate(pgtbl, 4*M+8*K, &attrs), 14*M+8*K);
    EXPECT_EQ(mmu_translate(pgtbl, 4*M+12*K, &attrs), 0);
    EXPECT_EQ(mmu_translate(pgtbl, 4*M+24*K, &attrs), 0);
    EXPECT_EQ(mmu_translate(pgtbl, 4*M+28*K, &attrs), 14*M+28*K);

    mmu_delete(pgtbl);
}


TEST_F(MmuTest, UnmapBreak1G) {
    size_t pgtbl = mmu_create();
    mmu_attr_t attrs;

    // map va [2G,8G) to pa [12G,18G)
    mmu_map(pgtbl, 2*G, 8*G, 12*G, MMU_WRITE);
    EXPECT_EQ(mmu_translate(pgtbl, 2*G, &attrs), 12*G);
    EXPECT_EQ(mmu_translate(pgtbl, 8*G-4*K, &attrs), 18*G-4*K);

    // unmap va [4G+12K,4G+28K)
    mmu_unmap(pgtbl, 4*G+12*K, 4*G+28*K);
    EXPECT_EQ(mmu_translate(pgtbl, 4*G+8*K, &attrs), 14*G+8*K);
    EXPECT_EQ(mmu_translate(pgtbl, 4*G+12*K, &attrs), 0);
    EXPECT_EQ(mmu_translate(pgtbl, 4*G+24*K, &attrs), 0);
    EXPECT_EQ(mmu_translate(pgtbl, 4*G+28*K, &attrs), 14*G+28*K);

    mmu_delete(pgtbl);
}
