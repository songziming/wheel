#include <gtest/gtest.h>

#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>

extern "C" {
    #include "fifo.h"
}

class FifoTest : public ::testing::Test {
public:
    char    buff_[1024];
    fifo_t  fifo_;
protected:
    void SetUp() override {
        fifo_init(&fifo_, buff_, sizeof(buff_));
    }
    void TearDown() override {}
};

TEST_F(FifoTest, ReadWrite) {
    const char STR[] = "hello, world!";
    size_t written = fifo_write(&fifo_, STR, 0, sizeof(STR));
    EXPECT_EQ(written, sizeof(STR));

    char readback[64];
    size_t read = fifo_read(&fifo_, readback, 0, sizeof(readback));
    EXPECT_EQ(read, sizeof(STR));
    EXPECT_STREQ(readback, STR);
}

// fifo 大小 1024
// 每次写 100B，可以写 10 次，第 11 次覆盖老数据
TEST_F(FifoTest, Rollback) {
    char rwbuf[100];

    EXPECT_TRUE(fifo_is_empty(&fifo_));

    for (int i = 0; i < 10; ++i) {
        memset(rwbuf, 'A' + i, sizeof(rwbuf));
        fifo_force_write(&fifo_, rwbuf, sizeof(rwbuf));
    }

    // 已经写了 1000B
    EXPECT_EQ(fifo_data_size(&fifo_), 1000);
    EXPECT_EQ(fifo_left_size(&fifo_), 24);
    EXPECT_FALSE(fifo_is_full(&fifo_));

    // 再写入 100B
    memset(rwbuf, 'Z', sizeof(rwbuf));
    fifo_force_write(&fifo_, rwbuf, sizeof(rwbuf));
    EXPECT_EQ(fifo_data_size(&fifo_), 1024);
    EXPECT_EQ(fifo_left_size(&fifo_), 0);
    EXPECT_TRUE(fifo_is_full(&fifo_));
}
