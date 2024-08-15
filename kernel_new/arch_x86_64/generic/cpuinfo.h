#ifndef CPUINFO_H
#define CPUINFO_H

#include <common.h>

#define CPU_FEATURE_PCID        0x0001
#define CPU_FEATURE_X2APIC      0x0002
#define CPU_FEATURE_TSC         0x0004
#define CPU_FEATURE_HT          0x0008  // 超线程
#define CPU_FEATURE_NX          0x0010
#define CPU_FEATURE_1G          0x0020
#define CPU_FEATURE_ARAT        0x0040  // APIC Timer 频率固定，与处理器睿频无关（即使处在 deep-C 状态）
#define CPU_FEATURE_ERMS        0x0080  // enhanced rep movsb/stosb
#define CPU_FEATURE_FSGSBASE    0x0100  // 支持读写 fsbase、gsbase 的专用指令
#define CPU_FEATURE_INVPCID     0x0200
#define CPU_FEATURE_SMEP        0x0400  // 特权模式禁止执行用户页面的指令（防代码注入）
#define CPU_FEATURE_SMAP        0x0800  // 特权模式禁止访问用户页面的数据（防数据注入）
#define CPU_FEATURE_FEEDBACK    0x1000  // 大小核架构下，支持硬件调度反馈
#define CPU_FEATURE_VMX         0x2000  // Intel VMX 虚拟化扩展
#define CPU_FEATURE_SVM         0x4000  // AMD SVM 虚拟化扩展

INIT_TEXT void parse_cpuinfo();



typedef struct cache_info {
    size_t line_size;
    size_t sets;        // 有多少个 set
    size_t ways;        // 每个 set 有多少 tag，0 表示全相连，-1 表示无效
    size_t total_size;
} cache_info_t;

extern CONST cache_info_t g_l1d_info;
extern CONST cache_info_t g_l1i_info;
extern CONST cache_info_t g_l2_info;
extern CONST cache_info_t g_l3_info;

#endif // CPUINFO_H
