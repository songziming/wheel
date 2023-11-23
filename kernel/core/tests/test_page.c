#include <test.h>
#include <stdlib.h>

#include "../sources/page.c"

static void *early_alloc_rw(size_t size) {
    return malloc(size);
}

static void setup() {
    page_init(1000);
}

static void teardown() {
    free(g_pages);
}

TEST_F(Page, Basic, setup, teardown) {
    //
}
