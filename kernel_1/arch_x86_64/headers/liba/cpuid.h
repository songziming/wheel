#ifndef LIBA_CPUID_H
#define LIBA_CPUID_H

#include <base.h>

extern CONST char g_cpu_vendor[12];
extern CONST char g_cpu_brand[48];

#define CPU_FEATURE_PCID    1
#define CPU_FEATURE_X2APIC  2
#define CPU_FEATURE_TSC     4
#define CPU_FEATURE_NX      8
#define CPU_FEATURE_1G      16
#define CPU_APIC_CONSTANT   32  // APIC Timer 频率固定，与处理器睿频无关
extern CONST uint32_t g_cpu_features;

typedef struct cache_info {
    size_t line_size;
    size_t sets;        // 有多少个 set
    size_t ways;        // 每个 set 有多少 tag，0 表示全相连，-1 表示无效
    size_t total_size;
} cache_info_t;

extern CONST cache_info_t g_l1i_info;
extern CONST cache_info_t g_l1d_info;
extern CONST cache_info_t g_l2_info;
extern CONST cache_info_t g_l3_info;

extern CONST int64_t g_bus_freq;
extern CONST int64_t g_tsc_freq;

INIT_TEXT void get_cpu_info();

#endif // LIBA_CPUID_H
