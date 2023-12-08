#include <arch_debug.h>
#include <arch_interface.h>
#include <debug.h>

#include <dev/serial.h>
#include <dev/console.h>
#include <dev/framebuf.h>

#include <liba/cpuid.h>


//------------------------------------------------------------------------------
// 打印调试输出
//------------------------------------------------------------------------------

// 字符模式的输出函数
void serial_console_puts(const char *s, size_t n) {
    serial_puts(s, n);
    console_puts(s, n);
}

// 图形模式的输出函数
void serial_framebuf_puts(const char *s, size_t n) {
    serial_puts(s, n);
    framebuf_puts(s, n);
}


//------------------------------------------------------------------------------
// 堆栈跟踪
//------------------------------------------------------------------------------

// AMD64 栈结构（向下生长）：
// high |     arg 8     |
//      |     arg 7     |
//      |  return addr  | <- 刚跳转之后的 RSP
//      |    old RBP    | <- RBP
//      |  local var x  |
//  low |  local var y  | <- RSP

int unwind_from(void **addrs, int max, uint64_t rbp) {
    int i = 0;

    for (; i < max; ++i) {
        uint64_t *frame = (uint64_t *)rbp;
        addrs[i] = (void *)frame[1];
        if (NULL == addrs[i]) {
            break;
        }
        rbp = frame[0];
    }

    return i;
}

// 定义在 arch_interface.h
int unwind(void **addrs, int max) {
    uint64_t rbp;
    __asm__("movq %%rbp, %0" : "=r"(rbp));
    return unwind_from(addrs, max, rbp);
}


//------------------------------------------------------------------------------
// 调试信息输出
//------------------------------------------------------------------------------

// 显示缓存参数
static void show_cache_info(const cache_info_t *info) {
    dbg_print("%ld/%ld/0x%lx",
            info->line_size, info->ways, info->total_size);
}

// 调试输出
void show_cpu_info() {
    // dbg_print("cpu vendor: '%.12s'\n", g_cpu_vendor);
    // dbg_print("cpu brand: '%.48s'\n", g_cpu_brand);

    struct {
        const char *name;
        uint32_t mask;
    } feats[] = {
        { "pcid",    CPU_FEATURE_PCID   },
        { "x2apic",  CPU_FEATURE_X2APIC },
        { "tsc",     CPU_FEATURE_TSC    },
        { "nx",      CPU_FEATURE_NX     },
        { "pdpe1gb", CPU_FEATURE_1G     }
    };
    int nfeats = sizeof(feats) / sizeof(feats[0]);

    // dbg_print("features:");
    for (int i = 0; i < nfeats; ++i) {
        if (g_cpu_features & feats[i].mask) {
            dbg_print(" %s", feats[i].name);
        }
    }

    dbg_print(", L1I "); show_cache_info(&g_l1i_info);
    dbg_print(", L1D "); show_cache_info(&g_l1d_info);
    // dbg_print("L2  "); show_cache_info(&g_l2_info);
    // dbg_print("L3  "); show_cache_info(&g_l3_info);
    dbg_print("\n");

    // dbg_print("bus-freq=%ld, tsc-freq=%ld\n", g_bus_freq, g_tsc_freq);
}
