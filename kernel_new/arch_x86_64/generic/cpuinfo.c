#include "cpuinfo.h"
#include "rw.h"
#include <string.h>
#include <debug.h>


// Linux kernel 代码中，cpuid 是一个设备文件，因此 cpuid.c 只能看到向系统注册设备，将文件读写转换为 cpuid 指令的逻辑
// 真正执行 cpuid 的代码位于 cpuflags.c，相关代码还有 cpucheck.c


#define VENDOR_INTEL "GenuineIntel"
#define VENDOR_AMD   "AuthenticAMD"

static CONST uint32_t g_max_eax;

static CONST uint32_t g_cpu_vendor[3];
// static CONST uint32_t g_cpu_brand[4];

// static CONST uint8_t g_cpu_stepping;
static CONST uint8_t g_cpu_model;
static CONST uint8_t g_cpu_family;
// static CONST uint8_t g_cpu_type;
// static CONST uint8_t g_cpu_ex_model;
// static CONST uint8_t g_cpu_ex_family;

CONST uint32_t g_cpu_features;

typedef struct cache_info {
    size_t line_size;
    size_t sets;        // 有多少个 set
    size_t ways;        // 每个 set 有多少 tag，0 表示全相连，-1 表示无效
    size_t total_size;
} cache_info_t;

CONST cache_info_t g_l1d_info;
CONST cache_info_t g_l1i_info;
CONST cache_info_t g_l2_info;
CONST cache_info_t g_l3_info;



//------------------------------------------------------------------------------
// AMD 获取缓存信息
//------------------------------------------------------------------------------

static INIT_TEXT void amd_parse_l1(uint32_t reg, cache_info_t *info) {
    size_t line_size     =  reg        & 0xff;
    size_t lines_per_tag = (reg >>  8) & 0xff;
    size_t ways          = (reg >> 16) & 0xff;
    size_t size          = (reg >> 24) & 0xff;
    size_t tag_size      = line_size * lines_per_tag;

    if (0 == tag_size) {
        return;
    }

    info->line_size  = tag_size;
    info->ways       = ways;
    info->total_size = (size_t)size << 10;

    size_t line_num  = info->total_size / info->line_size;

    if (0xff == info->ways) {
        info->sets = 1; // full associative，只有一个 set
        info->ways = line_num;
    } else {
        info->sets = line_num / info->ways;
    }
}

static INIT_TEXT size_t amd_l2l3_assoc(size_t assoc) {
    switch (assoc) {
    case  5: return     6; break;
    case  6: return     8; break;
    case  8: return    16; break;
    case 10: return    32; break;
    case 11: return    48; break;
    case 12: return    64; break;
    case 13: return    96; break;
    case 14: return   128; break;
    case 15: return     0; break;
    default: return assoc; break;
    }
}

// 返回 1 表示缓存信息无效，需要用 cpuid(0x8000001d)
static INIT_TEXT int amd_parse_l2(uint32_t reg, cache_info_t *info) {
    size_t line_size     =  reg        & 0xff;
    size_t lines_per_tag = (reg >>  8) & 0x0f;
    size_t assoc         = (reg >> 12) & 0x0f;
    size_t size          = (reg >> 16) & 0xffff; // KB
    size_t tag_size      = line_size * lines_per_tag;

    if ((9 == assoc) || (0 == tag_size)) {
        return 1;
    }

    info->line_size  = tag_size;
    info->ways       = amd_l2l3_assoc(assoc);
    info->total_size = (size_t)size << 10;

    size_t line_num  = info->total_size / info->line_size;

    if (0 == info->ways) {
        info->sets = 1;
        info->ways = line_num;
    } else {
        info->sets = line_num / info->ways;
    }

    return 0;
}

// 返回 1 表示缓存信息无效，需要用 cpuid(0x8000001d)
static INIT_TEXT int amd_parse_l3(uint32_t reg, cache_info_t *info) {
    size_t line_size     =  reg        & 0xff;
    size_t lines_per_tag = (reg >>  8) & 0x0f;
    size_t assoc         = (reg >> 12) & 0x0f;
    size_t size          = (reg >> 18) & 0x3fff; // 512KB
    size_t tag_size      = line_size * lines_per_tag;

    if ((9 == assoc) || (0 == tag_size)) {
        return 1;
    }

    info->line_size  = tag_size;
    info->ways       = amd_l2l3_assoc(assoc);
    info->total_size = (size_t)size << 19;

    size_t line_num  = info->total_size / info->line_size;

    if (0 == info->ways) {
        info->sets = 1;
        info->ways = line_num;
    } else {
        info->sets = line_num / info->ways;
    }

    return 0;
}

static INIT_TEXT void amd_get_cache_info() {
    uint32_t b, c, d;

    __asm__ volatile("cpuid" : "=c"(c), "=d"(d) : "a"(0x80000005) : "ebx");
    amd_parse_l1(c, &g_l1d_info);
    amd_parse_l1(d, &g_l1i_info);

    __asm__ volatile("cpuid" : "=c"(c), "=d"(d) : "a"(0x80000006) : "ebx");
    int l2_bad = amd_parse_l2(c, &g_l2_info);
    int l3_bad = amd_parse_l3(d, &g_l3_info);

    if (l2_bad) {
        __asm__ volatile("cpuid" : "=b"(b), "=c"(c) : "a"(0x8000001d), "c"(2) : "edx");
        g_l2_info.line_size  = (b & 0xfff) + 1;
        g_l2_info.sets       = (size_t)c + 1;
        g_l2_info.ways       = ((b >> 22) & 0x3ff) + 1;
        g_l2_info.total_size = g_l2_info.line_size * g_l2_info.sets * g_l2_info.ways;
    }

    if (l3_bad) {
        __asm__ volatile("cpuid" : "=b"(b), "=c"(c) : "a"(0x8000001d), "c"(3) : "edx");
        g_l3_info.line_size  = (b & 0xfff) + 1;
        g_l3_info.sets       = (size_t)c + 1;
        g_l3_info.ways       = ((b >> 22) & 0x3ff) + 1;
        g_l3_info.total_size = g_l3_info.line_size * g_l3_info.sets * g_l3_info.ways;
    }
}


//------------------------------------------------------------------------------
// Intel 获取缓存信息
//------------------------------------------------------------------------------

typedef enum cache_type {
    INVALID = 0,
    TLBI, TLBD,
    L1I, L1D,
    L2,
    L3,
} cache_type_t;

static INIT_TEXT void intel_parse_leaf2_byte(uint8_t byte) {
    cache_type_t type = INVALID;
    int size = -1;
    int way = -1;
    int line = -1;

    const int K = 1024;
    const int M = 1024 * 1024;

    switch (byte) {
    case 0x00:
    case 0xfe: // needs leaf 18
    case 0xff: // needs leaf 4
        return;

    // L1 指令缓存
    case 0x06: type = L1I; size =  8 * K; way = 4; line = 32; break;
    case 0x08: type = L1I; size = 16 * K; way = 4; line = 32; break;
    case 0x09: type = L1I; size = 32 * K; way = 4; line = 64; break;
    case 0x30: type = L1I; size = 32 * K; way = 8; line = 64; break;

    // L1 数据缓存
    case 0x0a: type = L1D; size =  8 * K; way = 2; line = 32; break;
    case 0x0c: type = L1D; size = 16 * K; way = 4; line = 32; break;
    case 0x0d: type = L1D; size = 16 * K; way = 4; line = 64; break;
    case 0x0e: type = L1D; size = 24 * K; way = 6; line = 64; break;
    case 0x2c: type = L1D; size = 32 * K; way = 8; line = 64; break;
    case 0x60: type = L1D; size = 16 * K; way = 8; line = 64; break;
    case 0x66: type = L1D; size =  8 * K; way = 4; line = 64; break;
    case 0x67: type = L1D; size = 16 * K; way = 4; line = 64; break;
    case 0x68: type = L1D; size = 32 * K; way = 4; line = 64; break;

    // L2，不区分指令与数据
    case 0x1d: type = L2; size = 128 * K; way =  2; line = 64; break;
    case 0x21: type = L2; size = 256 * K; way =  8; line = 64; break;
    case 0x24: type = L2; size =   1 * M; way = 16; line = 64; break;
    case 0x41: type = L2; size = 128 * K; way =  4; line = 32; break;
    case 0x42: type = L2; size = 256 * K; way =  4; line = 32; break;
    case 0x43: type = L2; size = 512 * K; way =  4; line = 32; break;
    case 0x44: type = L2; size =   1 * M; way =  4; line = 32; break;
    case 0x45: type = L2; size =   2 * M; way =  4; line = 32; break;
    case 0x48: type = L2; size =   3 * M; way = 12; line = 64; break;
    case 0x4e: type = L2; size =   6 * M; way = 24; line = 64; break;
    case 0x78: type = L2; size =   1 * M; way =  4; line = 64; break;
    case 0x79: type = L2; size = 128 * K; way =  8; line = 64; break;
    case 0x7a: type = L2; size = 256 * K; way =  8; line = 64; break;
    case 0x7b: type = L2; size = 512 * K; way =  8; line = 64; break;
    case 0x7c: type = L2; size =   1 * M; way =  8; line = 64; break;
    case 0x7d: type = L2; size =   2 * M; way =  8; line = 64; break;
    case 0x7f: type = L2; size = 512 * K; way =  2; line = 64; break;
    case 0x80: type = L2; size = 512 * K; way =  8; line = 64; break;
    case 0x82: type = L2; size = 256 * K; way =  8; line = 32; break;
    case 0x83: type = L2; size = 512 * K; way =  8; line = 32; break;
    case 0x84: type = L2; size =   1 * M; way =  8; line = 32; break;
    case 0x85: type = L2; size =   2 * M; way =  8; line = 32; break;
    case 0x86: type = L2; size = 512 * K; way =  4; line = 64; break;
    case 0x87: type = L2; size =   1 * M; way =  8; line = 64; break;

    // 这个描述符在不同型号CPU上含义不同(Intel Xeon processor MP, Family 0FH, Model 06H)
    // 需要已经获取型号信息
    case 0x49:
        type = ((0x0f == g_cpu_family) && (0x06 == g_cpu_model)) ? L3 : L2;
        size = 4 * M; way = 16; line = 64;
        break;

    // 预取缓存
    case 0xf0:
    case 0xf1:
        break;

    // 其他
    default:
        return;
    }

    cache_info_t info;
    info.line_size = line;
    info.ways = way;
    info.total_size = size;
    info.sets = size / line / way;

    switch (type) {
    case L1I: g_l1i_info = info; break;
    case L1D: g_l1d_info = info; break;
    case L2:  g_l2_info  = info; break;
    case L3:  g_l3_info  = info; break;
    default: break;
    }
}

static INIT_TEXT void intel_parse_leaf2(uint32_t reg) {
    if (0x80000000 & reg) {
        return;
    }
    intel_parse_leaf2_byte(reg & 0xff);
    reg >>= 8;
    intel_parse_leaf2_byte(reg & 0xff);
    reg >>= 8;
    intel_parse_leaf2_byte(reg & 0xff);
    reg >>= 8;
    intel_parse_leaf2_byte(reg & 0xff);
}

// 解析 CPUID leaf 4 调用结果
// 返回 1 表示 subleaf 无效，返回 0 表示 subleaf 有效
static INIT_TEXT int intel_parse_leaf4(uint32_t eax, uint32_t ebx, uint32_t ecx) {
    int type = eax & 0x1f;
    int level = (eax >> 5) & 0x07;

    cache_info_t *target = NULL;
    switch (level) {
    case 0:
        return 1;
    case 1:
        if (1 == type) {
            target = &g_l1d_info;
        } else {
            target = &g_l1i_info;
        }
        break;
    case 2:
        target = &g_l2_info;
        break;
    case 3:
        target = &g_l3_info;
        break;
    default:
        break;
    }

    // 解析各字段含义
    int line_size  = ( ebx        & 0xfff) + 1;
    int partitions = ((ebx >> 12) & 0x3ff) + 1;
    int ways       = ((ebx >> 22) & 0x3ff) + 1;
    int sets       = ecx + 1;

    target->line_size = line_size;
    target->ways = ways;
    target->sets = sets;
    target->total_size = ways * partitions * line_size * sets;

    return 0;
}

// 解析 CPUID leaf 18 调用结果
// 返回 1 表示 subleaf 无效，返回 0 表示 subleaf 有效
static INIT_TEXT int intel_parse_leaf18(uint32_t ebx, uint32_t ecx, uint32_t edx) {
    int type = edx & 0x1f;
    const char *type_name = "invalid";
    switch (type) {
    case 0: return 1;
    case 1: type_name = "data TLB"; break;
    case 2: type_name = "code TLB"; break;
    case 3: type_name = "unified TLB"; break;
    case 4: type_name = "load-only TLB"; break;
    case 5: type_name = "store-only TLB"; break;
    default: break;
    }

    int level = (edx >> 5) & 0x07;
    int ways = (ebx >> 16) & 0xffff;
    int sets = ecx;

    log("level %d %s cache, %d-way, %d sets\n", level, type_name, ways, sets);

    return 0;
}

static INIT_TEXT void intel_get_cache_info() {
    uint32_t a, b, c, d;

    // 调用 CPUID leaf 2
    __asm__ volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(2));
    a &= ~0xffU;
    intel_parse_leaf2(a);
    intel_parse_leaf2(b);
    intel_parse_leaf2(c);
    intel_parse_leaf2(d);

    // 通过 CPUID leaf 4 获取缓存参数
    // 新的 CPU 普遍通过这个 leaf 返回缓存信息
    // 循环遍历所有 subleaf（通过 ecx 传入）
    for (int n = 0; ; ++n) {
        __asm__ volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c) : "a"(4), "c"(n));
        if (intel_parse_leaf4(a, b, c)) {
            break;
        }
    }

    // 通过 CPUID leaf 18 获取 TLB 参数
    // 循环遍历所有 subleaf（通过 ecx 传入）
    uint32_t max_subleaf = 0xffffffff;
    for (uint32_t n = 0; n <= max_subleaf; ++n) {
        __asm__ volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(0x18), "c"(n));
        if (0 == n) {
            max_subleaf = a;
        }
        if (intel_parse_leaf18(b, c, d)) {
            break;
        }
    }
}


//------------------------------------------------------------------------------
// Intel 获取 CPU 拓扑结构
//------------------------------------------------------------------------------

static const char *DOMAIN_TYPE_NAMES[] = {
    "",
    "logical processor",    // 1
    "core",                 // 2
    "module",               // 3
    "tile",                 // 4
    "die",                  // 5
    "die group",            // 6
    "package",              // implied
};


static INIT_TEXT void intel_get_topology() {
    uint32_t a, b, c, d;

    // 首先尝试 cpuid eax=0x1f
    // 如果不支持，再尝试 cpuid eax=0x0b

    // 按照顺序 logical processor、core、die
    for (int domain = 0; ; ++domain) {
        __asm__ volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(0x18), "c"(domain));

        int type = (c >> 8) & 0xff;
        if (0 == type) {
            break;
        }

        log("%s, x2APIC ID shift %d\n", DOMAIN_TYPE_NAMES[type], a & 0x1f);
        log("%d logical processors within each %s\n", b & 0xffff, DOMAIN_TYPE_NAMES[type + 1]);
    }
}


//------------------------------------------------------------------------------
// AMD 检测虚拟化支持（SVM，Secure Virtual Machine，也称 AMD-V）
//------------------------------------------------------------------------------

static INIT_TEXT void amd_detect_svm() {
    uint32_t c;

    __asm__ volatile("cpuid" : "=c"(c) : "a"(0x80000001) : "ebx", "edx");
    g_cpu_features |= (c & (1U << 2)) ? CPU_FEATURE_SVM : 0;
}


//------------------------------------------------------------------------------
// Intel 检测虚拟化支持（VMX，也称 VT-d）
//------------------------------------------------------------------------------

static INIT_TEXT void intel_detect_vmx() {
    uint32_t c;

    __asm__ volatile("cpuid" : "=c"(c) : "a"(1) : "ebx", "edx");
    g_cpu_features |= (c & (1U << 5)) ? CPU_FEATURE_VMX : 0;
}


//------------------------------------------------------------------------------
// 获取处理器信息
//------------------------------------------------------------------------------

// 参考 linux/arch/x86/boot/cpuflags.c, 函数 get_cpuflags(void)
INIT_TEXT void parse_cpuinfo() {
    uint32_t a, c, d;

    __asm__ volatile("cpuid" : "=a"(g_max_eax), "=b"(g_cpu_vendor[0]), "=c"(g_cpu_vendor[2]), "=d"(g_cpu_vendor[1]) : "a"(0));

    // basic information
    g_cpu_features  = 0;
    __asm__ volatile("cpuid" : "=a"(a), "=c"(c), "=d"(d) : "a"(1) : "ebx");
    // g_cpu_stepping  =  a        & 0x0f;
    g_cpu_model     = (a >>  4) & 0x0f;
    g_cpu_family    = (a >>  8) & 0x0f;
    // g_cpu_type      = (a >> 12) & 0x03;
    // g_cpu_ex_model  = (a >> 16) & 0x0f;
    // g_cpu_ex_family = (a >> 20) & 0xff;
    g_cpu_features |= (c & (1U <<  5)) ? CPU_FEATURE_VMX    : 0;
    g_cpu_features |= (c & (1U << 17)) ? CPU_FEATURE_PCID   : 0;
    g_cpu_features |= (c & (1U << 21)) ? CPU_FEATURE_X2APIC : 0;
    g_cpu_features |= (d & (1U <<  4)) ? CPU_FEATURE_TSC    : 0;
    g_cpu_features |= (d & (1U << 28)) ? CPU_FEATURE_HT     : 0;
    // if (g_cpu_features & CPU_FEATURE_HT) {
    //     g_num_ids = (b >> 16) & 0xff;
    // }

     // extended function
    __asm__ volatile("cpuid" : "=d"(d) : "a"(0x80000001) : "ebx", "ecx");
    g_cpu_features |= (d & (1U << 20)) ? CPU_FEATURE_NX : 0;
    g_cpu_features |= (d & (1U << 26)) ? CPU_FEATURE_1G : 0;

    // thermal and power
    __asm__ volatile("cpuid" : "=a"(a) : "a"(6) : "ebx", "ecx", "edx");
    g_cpu_features |= (a & (1U <<  2)) ? CPU_FEATURE_ARAT     : 0;
    g_cpu_features |= (a & (1U << 19)) ? CPU_FEATURE_FEEDBACK : 0;

    // // core crystal clock
    // __asm__ volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c) : "a"(0x15) : "edx");
    // g_core_freq = c;
    // g_tsc_ratio[0] = b;
    // g_tsc_ratio[1] = a;

    // 获取各级缓存信息，获取方式与 vendor 有关
    if (0 == memcmp(g_cpu_vendor, VENDOR_INTEL, 12)) {
        intel_get_cache_info();
        intel_get_topology();
        intel_detect_vmx();
    } else if (0 == memcmp(g_cpu_vendor, VENDOR_AMD, 12)) {
        amd_get_cache_info();
        amd_detect_svm();
    } else {
        log("unknown vendor name '%.12s'\n", g_cpu_vendor);
    }
}
