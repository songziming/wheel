#ifndef TEST_MMU_HELPER_H
#define TEST_MMU_HELPER_H

#include <page.h>

void mock_info_set(size_t key);
page_info_t *mock_info_get(size_t key);
void mock_info_clear(size_t key);

#endif // TEST_MMU_HELPER_H