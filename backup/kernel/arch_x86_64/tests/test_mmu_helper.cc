#include <map>
#include <vector>

extern "C" {
#include "test_mmu_helper.h"
}

// 利用 map 存储物理页的额外信息
static std::map<size_t, page_info_t*> infomap;

void mock_info_set(size_t key, page_info_t *info) {
    // page_info_t info;
    // info.ent_num = 999;
    infomap[key] = info;
}

page_info_t *mock_info_get(size_t key) {
    return infomap.at(key);
}

void mock_info_clear(size_t key) {
    infomap.erase(key);
}


#if 0

typedef struct map_range {
    size_t va;
    size_t end;
    size_t pa;
} map_range_t;

// 记录映射范围信息
static std::vector<map_range_t> ranges;

void add_map_range(uint64_t va, uint64_t pa, uint64_t size, uint64_t attr, int nitems) {
    map_range_t mrange;
    mrange.va  = va;
    mrange.end = va + size;
    mrange.pa  = pa;
    ranges.push_back(mrange);
}

#endif
