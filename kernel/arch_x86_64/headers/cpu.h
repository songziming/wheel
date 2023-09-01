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

INIT_TEXT void get_cpu_info();
INIT_TEXT void cpu_feat_init();

#ifdef DEBUG
INIT_TEXT void cpu_features_show();
#endif

#endif // CPU_H
