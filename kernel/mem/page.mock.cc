#include "page.mock.h"
#include <sys/mman.h>
#include <early_alloc.mock.h>

extern "C" {
    #include <arch_config.h>
    #include <page.h>
}

uint64_t g_direct_map_base;

PageMock::PageMock(size_t npages) : npages_(npages) {}

void PageMock::SetUp() {
    int prot = PROT_READ | PROT_WRITE;
    int flags = MAP_PRIVATE | MAP_ANONYMOUS;
    va_ = mmap(nullptr, npages_ << PAGE_SHIFT, prot, flags, -1, 0);
    ASSERT_NE(va_, MAP_FAILED);

    g_direct_map_base = (uint64_t)va_ - PAGE_SIZE;

    page_init(PAGE_SIZE, (npages_ + 1) << PAGE_SHIFT);
    pages_add(PAGE_SIZE, (npages_ + 1) << PAGE_SHIFT);
}

void PageMock::TearDown() {
    if (va_) {
        munmap(va_, npages_ << PAGE_SHIFT);
    }
    clear_early_chunks();
}
