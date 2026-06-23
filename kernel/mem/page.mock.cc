#include "page.mock.h"
#include <sys/mman.h>
#include <gtest/gtest.h>
#include <early_alloc.mock.h>

extern "C" {
    #include <page.h>
    #include <arch_config.h>
    uint64_t g_idmap_base;
}

PageContext::PageContext(size_t npages) : npages_(npages) {
    int prot = PROT_READ | PROT_WRITE;
    int flags = MAP_PRIVATE | MAP_ANONYMOUS;
    va_ = mmap(nullptr, npages_ << PAGE_SHIFT, prot, flags, -1, 0);
    // ASSERT_NE(va_, MAP_FAILED);

    g_idmap_base = (uint64_t)va_ - PAGE_SIZE;

    page_init(PAGE_SIZE, (npages_ + 1) << PAGE_SHIFT);
    pages_add(PAGE_SIZE, (npages_ + 1) << PAGE_SHIFT);
}

PageContext::~PageContext() {
    if (va_) {
        munmap(va_, npages_ << PAGE_SHIFT);
    }
    // clear_early_chunks();
}
