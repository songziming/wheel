#ifndef CPU_H
#define CPU_H

#include <base.h>

#define CPU_FEATURE_PCID            0x0001
#define CPU_FEATURE_X2APIC          0x0002
#define CPU_FEATURE_TSC             0x0004
#define CPU_FEATURE_NX              0x0008
#define CPU_FEATURE_1G              0x0010
#define CPU_FEATURE_APIC_CONSTANT   0x0020  // APIC Timer 频率固定，与处理器睿频无关
#define CPU_FEATURE_ERMS            0x0040  // enhanced rep movsb/stosb
#define CPU_FEATURE_INVPCID         0x0080

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

INIT_TEXT void cpu_info_detect();
#ifdef DEBUG
INIT_TEXT void cpu_info_show();
#endif
INIT_TEXT void cpu_features_init();


#endif // CPU_H
