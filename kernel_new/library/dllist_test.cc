#include <gtest/gtest.h>

extern "C" {
    #include "dllist.h"
}


TEST(DlList, Sample1) {
    dlnode_t head;
    dlnode_t node1;
    dlnode_t node2;

    dl_init_circular(&head);
    EXPECT_EQ(head.prev, &head);
    EXPECT_EQ(head.next, &head);
}
