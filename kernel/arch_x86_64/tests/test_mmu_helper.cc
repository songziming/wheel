#include <map>

extern "C" {
#include "test_mmu_helper.h"
}

// 利用 map 存储物理页的额外信息

std::map<size_t, page_info_t> infomap;

void mock_info_set(size_t key) {
    infomap[key] = page_info_t{};
}

page_info_t *mock_info_get(size_t key) {
    return &infomap.at(key);
}

void mock_info_clear(size_t key) {
    infomap.erase(key);
}
