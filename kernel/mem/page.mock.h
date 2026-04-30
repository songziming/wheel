#ifndef PAGE_MOCK_H
#define PAGE_MOCK_H

#include <gtest/gtest.h>
#include <stdint.h>

extern uint64_t g_direct_map_base;

class PageMock : public ::testing::Test {
    const size_t npages_;
    void *va_ = nullptr;

public:
    explicit PageMock(size_t npages);

protected:
    void SetUp() override;
    void TearDown() override;
};

#endif // PAGE_MOCK_H
