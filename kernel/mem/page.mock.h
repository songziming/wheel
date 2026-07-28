#ifndef PAGE_MOCK_H
#define PAGE_MOCK_H

#include <stddef.h>
#include <stdint.h>

// 需要用到物理内存的测试项，都需要创建这个类的实例
class PageContext {
    const size_t npages_;
    void *va_ = nullptr;

public:
    explicit PageContext(size_t npages);
    ~PageContext();

    PageContext(const PageContext&) = delete;
    PageContext& operator=(const PageContext&) = delete;
};

#endif // PAGE_MOCK_H
