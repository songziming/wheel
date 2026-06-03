#ifndef ARCH_X86_64_CPU_FEATURES_H
#define ARCH_X86_64_CPU_FEATURES_H

#include <wheel.h>

#define CPU_FEATURE_PCID        0x00001
#define CPU_FEATURE_X2APIC      0x00002
#define CPU_FEATURE_TSC         0x00004
#define CPU_FEATURE_HT          0x00008  // 超线程
#define CPU_FEATURE_NX          0x00010
#define CPU_FEATURE_1G          0x00020
#define CPU_FEATURE_ARAT        0x00040  // APIC Timer 频率固定，与处理器睿频无关（即使处在 deep-C 状态）
#define CPU_FEATURE_ERMS        0x00080  // enhanced rep movsb/stosb
#define CPU_FEATURE_FSGSBASE    0x00100  // 支持读写 fsbase、gsbase 的专用指令
#define CPU_FEATURE_INVPCID     0x00200
#define CPU_FEATURE_SMEP        0x00400  // 特权模式禁止执行用户页面的指令（防代码注入）
#define CPU_FEATURE_SMAP        0x00800  // 特权模式禁止访问用户页面的数据（防数据注入）
#define CPU_FEATURE_FEEDBACK    0x01000  // 大小核架构下，支持硬件调度反馈
#define CPU_FEATURE_VMX         0x02000  // Intel VMX 虚拟化扩展
#define CPU_FEATURE_SVM         0x04000  // AMD SVM 虚拟化扩展
#define CPU_FEATURE_TSC_FIXED   0x08000  // TSC 频率固定
#define CPU_FEATURE_TSC_ADJUST  0x10000  // 支持 TSC-ADJUST 相位控制
#define CPU_FEATURE_TSC_DDL     0x20000  // APIC Timer 支持 tsc deadline 模式
#define CPU_FEATURE_VTD         0x40000  // Intel VT-d I/O 虚拟化（DMAR 表存在）

typedef struct cache_info {
    size_t line_size;
    size_t sets;        // 有多少个 set
    size_t ways;        // 每个 set 有多少 tag，0 表示全相连，-1 表示无效
    size_t total_size;
} cache_info_t;

extern cache_info_t g_l1d_info;
extern cache_info_t g_l1i_info;
extern cache_info_t g_l2_info;
extern cache_info_t g_l3_info;
extern uint32_t g_cpu_features;

INIT_TEXT void cpu_features_detect();
INIT_TEXT void cpu_features_enable();

#endif // ARCH_X86_64_CPU_FEATURES_H
