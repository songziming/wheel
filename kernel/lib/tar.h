#ifndef TAR_H
#define TAR_H

#include <stddef.h>

// 返回 1 表示继续遍历，返回 0 表示停止迭代
typedef int (*tar_cb)(const char *file, const char *data, size_t len, void *user);

void tar_iterate(const void *data_base, size_t tar_size, tar_cb cb, void *user);

#endif // TAR_H
