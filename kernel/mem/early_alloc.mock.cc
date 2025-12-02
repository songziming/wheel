#include <stddef.h>
#include <stdint.h>
#include <vector>

struct EarlyAlloc {
    std::vector<uint64_t*> regions;

    explicit EarlyAlloc() {}
    virtual ~EarlyAlloc() { clean(); }

    void clean() {
        for (const auto &ptr : regions) {
            delete[] ptr;
        }
        regions.clear();
    }
};

static EarlyAlloc g_early;

void clear_early_chunks() {
    g_early.clean();
}

extern "C" void *early_alloc_rw(size_t size) {
    size += 7;
    size >>= 3;
    uint64_t *ptr = new uint64_t[size];
    g_early.regions.push_back(ptr);
    return ptr;
}

extern "C" void *early_alloc_ro(size_t size) __attribute__((alias("early_alloc_rw")));
