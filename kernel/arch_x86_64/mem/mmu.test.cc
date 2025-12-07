#include <gtest/gtest.h>
#include <sys/mman.h>
#include <early_alloc.mock.h>

extern "C" {
    #include <arch_api.h>
    #include <page.h>
    #include <cpu/features.h>
}

#define K  1024L
#define M (1024L*K)
#define G (1024L*M)

uint64_t g_direct_map_base;

// 使用 mmap 申请一段内存，模拟物理内存
class MmuTest : public ::testing::Test {
    const size_t npages = 16 * K; // 预留 64M 空间
    void *va_ = nullptr;

protected:
    void SetUp() override {
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



TEST_F(MmuTest, Mapping) {
    size_t pgtbl = mmu_create();

    mmu_map(pgtbl, 2*G, 3*G, 1*G, MMU_WRITE);  // 2G~3G --> 1G
    mmu_map(pgtbl, 4*M, 6*M, 2*M, MMU_WRITE);  // 4M~6M --> 2M
    mmu_map(pgtbl, 4*K, 8*K, 8*K, MMU_WRITE);  // 4K~8K --> 8K

    mmu_delete(pgtbl);
}
