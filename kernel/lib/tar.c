// 简易 tar 解析器
// tar 格式：每个 entry 一个 512 字节 header + 数据（512 对齐）

#include "tar.h"
#include <kstring.h>
#include <debug.h>

#define TAR_BLKSZ 512

// 将 octal 字符串转为整数（只解析纯数字，不处理 NUL 和空格）
static size_t tar_oct2int(const char *s, int len) {
    size_t val = 0;
    for (int i = 0; i < len && s[i]; i++) {
        if (s[i] < '0' || s[i] > '7') break;
        val = (val << 3) | (size_t)(s[i] - '0');
    }
    return val;
}


void tar_iterate(const void *data_base, size_t tar_size, tar_cb cb, void *user) {
    const char *ptr = (const char*)data_base;
    const char *end = ptr + tar_size;

    // 按 block-size 遍历
    while (ptr + TAR_BLKSZ <= end) {
        const char *name = ptr;

        // 检查空 header：name[0] == '\0' 表示结束
        // TODO tar 要求连续两个空块表示结束
        if ('\0' == name[0]) {
            return;
        }

        size_t fsize = tar_oct2int(ptr + 124, 12);
        const char *data = ptr + TAR_BLKSZ;

        // 检查数据是否在 tar 范围内
        if (data + fsize > end) {
            logk("tar: entry '%s' data out of range\n", name);
            break;
        }

        if (0 == cb(name, data, fsize, user)) {
            return;
        }
        // logk("tar-entry name=`%s`, size=%zu, ptr=%p\n", name, fsize, data);

        // 跳到下一个 entry：header + data（512 对齐）
        fsize += TAR_BLKSZ - 1;
        fsize &= ~(TAR_BLKSZ - 1);
        ptr = data + fsize;
    }
}
