#ifndef WHEEL_H
#define WHEEL_H

#include "defs.h"
#include <arch_config.h>
#include "arch_api.h"
#include <arch_extra.h>


// 调试输出，将文本记录到文件，并通过串口打印
void klog(const char *fmt, ...);

// 打印调试输出
// klog 需要 arch 支持，每个平台上打印输出的逻辑各不相同
// vmshutdown 也一样，取决于运行在哪种虚拟机上
#if defined(UNIT_TEST)
    #include <assert.h>
    #define ASSERT assert
#elif defined(DEBUG)
    #define ASSERT(x) do { \
        if (!(x)) { \
            klog("Assert failed %s:%s:%d\n", __FILE__, __func__, __LINE__); \
            vmshutdown(1); \
        } \
    } while (0)
#else
    #define ASSERT(...)
#endif

#endif // WHEEL_H
