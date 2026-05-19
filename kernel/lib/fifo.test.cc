#include <gtest/gtest.h>

#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>

extern "C" {
    #include "fifo.h"
}

class FifoTest : public ::testing::Test {
public:
    size_t  size_{4096};
    fifo_t  fifo_;
protected:
    void SetUp() override {
        int fd = shm_open("/ringbuf_test", O_RDWR|O_CREAT|O_TRUNC, 0600);
        ftruncate(fd, size_);

        // 预留 2x 虚拟地址空间
        char *addr = (char*)mmap(NULL, size_*2, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);

        // 后半段解除，重新映射到同一个文件偏移 0
        munmap(addr + size_, size_);
        mmap(addr + size_, size_, PROT_READ|PROT_WRITE, MAP_SHARED|MAP_FIXED, fd, 0);

        close(fd);
        shm_unlink("/ringbuf_test");

        fifo_init(&fifo_, addr, size_);
    }
    void TearDown() override {
        munmap(fifo_.data, fifo_.size * 2);
    }
};

TEST_F(FifoTest, ReadWrite) {
    char rwbuf[1024];

    const char STR[] = "hello, world!";
    size_t written = fifo_write(&fifo_, STR, 0, sizeof(STR));
    EXPECT_EQ(written, sizeof(STR));

    size_t read = fifo_read(&fifo_, rwbuf, 0, sizeof(rwbuf));
    EXPECT_EQ(read, sizeof(STR));
    EXPECT_STREQ(rwbuf, STR);
}

static const char LOREM[] = "Lorem ipsum dolor sit amet, consectetur adipiscing"
"elit. Donec sollicitudin tellus quis odio mattis ultrices. Cras diam magna,"
"pharetra in justo non, semper pulvinar lorem. Quisque pellentesque laoreet"
"viverra. In id consequat massa, eu lacinia lectus. Curabitur in metus turpis."
"Ut orci ligula, suscipit et risus ac, ultrices malesuada urna. Vestibulum et"
"vestibulum turpis. Morbi justo leo, rutrum eget fermentum in, vestibulum ornare"
"quam. Mauris iaculis blandit ex vel laoreet. Donec id arcu vitae odio porttitor"
"condimentum. Curabitur eget congue mauris. Mauris purus odio, lobortis efficitur"
"nisl sit amet, tempus suscipit sem. Aenean nec dictum urna. In ut lobortis urna,"
"at posuere urna. Nullam quam elit, imperdiet vel ipsum eleifend, pellentesque"
"volutpat elit. Praesent mattis felis in iaculis ullamcorper. Maecenas in elit"
"sed odio pulvinar mattis non eu diam. Curabitur feugiat velit in mi pretium"
"ultrices. Suspendisse eu metus nec nulla hendrerit pulvinar sit amet sed risus."
"Curabitur cursus tincidunt convallis. Nunc augue libero, euismod in luctus sed,"
"eleifend non lacus. Ut quis hendrerit nisl. Nam eleifend eros eu commodo"
"venenatis. Aliquam ex augue, sollicitudin vitae nulla vel, aliquam iaculis nunc."
"Pellentesque auctor nunc vitae quam dapibus elementum. In hac habitasse platea"
"dictumst. Mauris et lorem efficitur, tempus nulla vitae, cursus libero."
"Pellentesque enim est, aliquam nec luctus luctus, rhoncus quis mauris. Fusce"
"commodo posuere nisi, nec tristique libero suscipit vel. Phasellus cursus sapien"
"accumsan ullamcorper commodo. Praesent nec ipsum at eros molestie ullamcorper."
"Fusce venenatis feugiat nisl quis aliquet. Curabitur semper, odio nec commodo"
"sodales, dui orci consectetur purus, in tristique nulla erat in leo. Mauris"
"porta pulvinar nisl at tempor. Pellentesque habitant morbi tristique senectus et"
"netus et malesuada fames ac turpis egestas. Nulla at nulla elementum, bibendum"
"augue sed, elementum nisi. Phasellus lacus justo, sollicitudin a sapien at,"
"faucibus facilisis diam.";


TEST_F(FifoTest, Rollback) {
    size_t total = 0;
    while (total < size_) {
        fifo_force_write(&fifo_, LOREM, sizeof(LOREM));
        total += sizeof(LOREM);
    }
    EXPECT_EQ(fifo_data_size(&fifo_), size_);
}
