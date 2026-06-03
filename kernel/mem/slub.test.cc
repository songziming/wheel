#include <gtest/gtest.h>
#include "page.mock.h"
#include <early_alloc.mock.h>

extern "C" {
    #include "slub.h"
    #include <arch_config.h>
}


// SLUB 单元测试，使用 PageMock 提供模拟物理内存
class SlubTest : public PageMock {
protected:
    SlubTest() : PageMock(256) {}
};

TEST_F(SlubTest, AllocAndFree) {
    slub_t slub;
    slub_init(&slub, 64);

    // 分配 8 个对象
    void *objs[8];
    for (int i = 0; i < 8; ++i) {
        objs[i] = slub_alloc(&slub);
        ASSERT_NE(objs[i], nullptr);
        // 写入测试模式
        *(uint64_t *)objs[i] = 0xDEADBEEF00000000UL + i;
    }

    // 8 个对象应该互不相同
    for (int i = 0; i < 8; ++i) {
        for (int j = i + 1; j < 8; ++j) {
            EXPECT_NE(objs[i], objs[j]);
        }
    }

    // 释放中间 4 个
    for (int i = 2; i < 6; ++i) {
        slub_free(&slub, objs[i]);
    }

    // 再分配 4 个
    void *new_objs[4];
    for (int i = 0; i < 4; ++i) {
        new_objs[i] = slub_alloc(&slub);
        ASSERT_NE(new_objs[i], nullptr);
        *(uint64_t *)new_objs[i] = 0xCAFE000000000000UL + i;
    }

    // 验证未释放的 4 个对象数据未损坏
    EXPECT_EQ(*(uint64_t *)objs[0], 0xDEADBEEF00000000UL + 0);
    EXPECT_EQ(*(uint64_t *)objs[1], 0xDEADBEEF00000000UL + 1);
    EXPECT_EQ(*(uint64_t *)objs[6], 0xDEADBEEF00000000UL + 6);
    EXPECT_EQ(*(uint64_t *)objs[7], 0xDEADBEEF00000000UL + 7);

    slub_destroy(&slub);
}
