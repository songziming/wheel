#include <gtest/gtest.h>
#include <sys/mman.h>
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

uint64_t g_direct_map_base;

// static void mock_logk(const char *s, size_t n) {
//     std::string_view sv(s);
//     std::cerr << sv.substr(0, n);
// }

// 使用 mmap 申请一段内存，模拟物理内存
class MmuTest : public ::testing::Test {
    const size_t npages = 16 * K; // 预留 64M 空间
    void *va_ = nullptr;

protected:
    void SetUp() override {
        // g_log_func = mock_logk;

        int prot = PROT_READ|PROT_WRITE;
        int flags = MAP_PRIVATE|MAP_ANONYMOUS;
        va_ = mmap(nullptr, npages << PAGE_SHIFT, prot, flags, -1, 0);
        g_direct_map_base = (uint64_t)va_ - PAGE_SIZE;

        page_init(PAGE_SIZE, (npages + 1) << PAGE_SHIFT);
        pages_add(PAGE_SIZE, (npages + 1) << PAGE_SHIFT);

        // 允许使用 1G 映射，支持 NX 标记
        g_cpu_features |= CPU_FEATURE_1G | CPU_FEATURE_NX;
    }

    void TearDown() override {
        munmap(va_, npages << PAGE_SHIFT);
        clear_early_chunks(); // 页描述符数组的空间也要回收
    }
};


// 测试各级映射
TEST_F(MmuTest, Mapping) {
    size_t pgtbl = mmu_create();
    mmu_attr_t attrs;

    EXPECT_EQ(mmu_translate(pgtbl, 1*G, &attrs), 0);

    // map va [2G,3G) to pa [1G,2G)
    mmu_map(pgtbl, 2*G, 3*G, 1*G, MMU_WRITE);
    EXPECT_EQ(mmu_translate(pgtbl, 2*G-4*K, &attrs), 0);
    EXPECT_EQ(mmu_translate(pgtbl, 2*G, &attrs), 1*G);
    EXPECT_EQ(mmu_translate(pgtbl, 3*G - 4*K, &attrs), 2*G - 4*K);
    EXPECT_EQ(mmu_translate(pgtbl, 3*G, &attrs), 0);

    // map va [4M,6M) to pa [2M,4M)
    mmu_map(pgtbl, 4*M, 6*M, 2*M, MMU_WRITE);
    EXPECT_EQ(mmu_translate(pgtbl, 4*M - 4*K, &attrs), 0);
    EXPECT_EQ(mmu_translate(pgtbl, 4*M, &attrs), 2*M);
    EXPECT_EQ(mmu_translate(pgtbl, 6*M - 4*K, &attrs), 4*M - 4*K);
    EXPECT_EQ(mmu_translate(pgtbl, 6*M, &attrs), 0);

    // map va [4K,12K) to pa [8K,16K)
    mmu_map(pgtbl, 4*K, 12*K, 8*K, MMU_WRITE);
    EXPECT_EQ(mmu_translate(pgtbl, 0, &attrs), 0);
    EXPECT_EQ(mmu_translate(pgtbl, 4*K, &attrs), 8*K);
    EXPECT_EQ(mmu_translate(pgtbl, 8*K, &attrs), 12*K);
    EXPECT_EQ(mmu_translate(pgtbl, 12*K, &attrs), 0);

    mmu_delete(pgtbl);
}


// 破环原本的映射，检查原本的映射能否保留
TEST_F(MmuTest, PartialRemap2M) {
    size_t pgtbl = mmu_create();
    mmu_attr_t attrs;

    // map va [2M,8M) to pa [12M,18M)
    mmu_map(pgtbl, 2*M, 8*M, 12*M, MMU_WRITE);
    EXPECT_EQ(mmu_translate(pgtbl, 2*M, &attrs), 12*M);
    EXPECT_EQ(mmu_translate(pgtbl, 6*M, &attrs), 16*M);

    // map va [4M+12K,4M+28K) to pa [32K,48K)
    mmu_map(pgtbl, 4*M+12*K, 4*M+28*K, 32*K, MMU_WRITE);

    // check new mapping
    EXPECT_EQ(mmu_translate(pgtbl, 4*M+12*K, &attrs), 32*K);
    EXPECT_EQ(mmu_translate(pgtbl, 4*M+24*K, &attrs), 44*K);

    // check old remaining mapping
    EXPECT_EQ(mmu_translate(pgtbl, 4*M+8*K, &attrs), 14*M+8*K);
    EXPECT_EQ(mmu_translate(pgtbl, 4*M+28*K, &attrs), 14*M+28*K);

    mmu_delete(pgtbl);
}


// 取消映射，但是破坏 big-page
TEST_F(MmuTest, PartialUnmap2M) {
    size_t pgtbl = mmu_create();
    mmu_delete(pgtbl);
}
