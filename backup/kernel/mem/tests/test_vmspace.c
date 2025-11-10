#include <test.h>

#include "../sources/context.c"

TEST(Context, Sorted) {
    context_t ctx;
    ctx.spin.ticket_counter = 1;
    ctx.spin.service_counter = 0;
    dl_init_circular(&ctx.head);

    vmrange_t rng1 = { .addr = 0x1000, .end = 0x2000 };
    vm_insert(&ctx, &rng1);

    vmrange_t rng3 = { .addr = 0x3000, .end = 0x4000 };
    vm_insert(&ctx, &rng3);

    vmrange_t rng2 = { .addr = 0x2000, .end = 0x3000 };
    vm_insert(&ctx, &rng2);

    EXPECT_TRUE(rng1.dl.next == &rng2.dl);
    EXPECT_TRUE(rng2.dl.prev == &rng1.dl);

    EXPECT_TRUE(rng2.dl.next == &rng3.dl);
    EXPECT_TRUE(rng3.dl.prev == &rng2.dl);
}
