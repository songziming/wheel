#include <gtest/gtest.h>

extern "C" {
    #include "page.h"
    #include <arch_api.h>
}

TEST(Page, AllocFree) {
    page_init(0, 100*PAGE_SIZE);
    EXPECT_EQ(g_page_start, 1);
}
