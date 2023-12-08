// 使用 cpuid 指令获取 CPU 规格
// 包括一些新功能是否支持、缓存大小
// 缓存信息可用于指导内存分配、页面着色

// 64-bit 模式下，CPUID会将寄存器高 32-bit 清零


#include <liba/cpuid.h>

#include <debug.h>
#include <libk.h>


#define VENDOR_INTEL "GenuineIntel"
#define VENDOR_AMD "AuthenticAMD"

CONST char g_cpu_vendor[12];
CONST char g_cpu_brand[48];

static CONST int cpu_stepping;
static CONST int cpu_model;
static CONST int cpu_family;
static CONST int cpu_type;
static CONST int cpu_ex_model;
static CONST int cpu_ex_family;

CONST uint32_t g_cpu_features;

CONST cache_info_t g_l1i_info = {0};
CONST cache_info_t g_l1d_info = {0};
CONST cache_info_t g_l2_info  = {0};
CONST cache_info_t g_l3_info  = {0};


//------------------------------------------------------------------------------
// 获取基本信息
//------------------------------------------------------------------------------

static INIT_TEXT void get_basic_info() {
    uint32_t eax;
    uint32_t ecx;
    uint32_t edx;

    // 获取 vendor string
    __asm__("cpuid" : "=b"(*(uint32_t *)&g_cpu_vendor[0]),
                      "=c"(*(uint32_t *)&g_cpu_vendor[8]),
                      "=d"(*(uint32_t *)&g_cpu_vendor[4])
                    : "a"(0));

    // 获取 processor brand
    __asm__("cpuid" : "=a"(*(uint32_t *)&g_cpu_brand[0]),
                      "=b"(*(uint32_t *)&g_cpu_brand[4]),
                      "=c"(*(uint32_t *)&g_cpu_brand[8]),
                      "=d"(*(uint32_t *)&g_cpu_brand[12])
                    : "a"(0x80000002));
    __asm__("cpuid" : "=a"(*(uint32_t *)&g_cpu_brand[16]),
                      "=b"(*(uint32_t *)&g_cpu_brand[20]),
                      "=c"(*(uint32_t *)&g_cpu_brand[24]),
                      "=d"(*(uint32_t *)&g_cpu_brand[28])
                    : "a"(0x80000003));
    __asm__("cpuid" : "=a"(*(uint32_t *)&g_cpu_brand[32]),
                      "=b"(*(uint32_t *)&g_cpu_brand[36]),
                      "=c"(*(uint32_t *)&g_cpu_brand[40]),
                      "=d"(*(uint32_t *)&g_cpu_brand[44])
                    : "a"(0x80000004));

    uint32_t features = 0;

    // 获取型号和功能特性
    __asm__("cpuid" : "=a"(eax), "=c"(ecx), "=d"(edx) : "a"(1) : "ebx");
    cpu_stepping  =  eax        & 0x0f;
    cpu_model     = (eax >>  4) & 0x0f;
    cpu_family    = (eax >>  8) & 0x0f;
    cpu_type      = (eax >> 12) & 0x03;
    cpu_ex_model  = (eax >> 16) & 0x0f;
    cpu_ex_family = (eax >> 20) & 0xff;
    features |= (ecx & (1U << 17)) ? CPU_FEATURE_PCID   : 0;
    features |= (ecx & (1U << 21)) ? CPU_FEATURE_X2APIC : 0;
    features |= (edx & (1U <<  4)) ? CPU_FEATURE_TSC    : 0;

    __asm__("cpuid" : "=d"(edx) : "a"(0x80000001) : "ebx", "ecx");
    features |= (edx & (1U << 20)) ? CPU_FEATURE_NX     : 0;
    features |= (edx & (1U << 26)) ? CPU_FEATURE_1G     : 0;

    __asm__("cpuid" : "=a"(eax) : "a"(6) : "ebx", "ecx", "edx");
    features |= (eax & (1U << 2)) ? CPU_APIC_CONSTANT : 0;

    g_cpu_features = features;
}


//------------------------------------------------------------------------------
// 获取缓存信息 -- AMD
//------------------------------------------------------------------------------

static INIT_TEXT void parse_amd_l1(uint32_t reg, cache_info_t *info) {
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
        // full associative 相当于只有一个 set
        info->sets = 1;
        info->ways = line_num;
    } else {
        info->sets = line_num / info->ways;
    }
}

// 返回每个 set 有多少 line，0 表示 full-associative
// 输入 9 是无效值，caller 需要提前处理
static INIT_TEXT size_t parse_l2l3_assoc(size_t assoc) {
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

// 返回 1 表示信息无效，需要使用 cpuid(0x8000_001d) 获取缓存信息
static INIT_TEXT int parse_amd_l2(uint32_t reg, cache_info_t *info) {
    size_t line_size     =  reg        & 0xff;
    size_t lines_per_tag = (reg >>  8) & 0x0f;
    size_t assoc         = (reg >> 12) & 0x0f;
    size_t size          = (reg >> 16) & 0xffff;
    size_t tag_size      = line_size * lines_per_tag;

    if ((9 == assoc) || (0 == tag_size)) {
        return 1;
    }

    info->line_size = tag_size;
    info->ways = parse_l2l3_assoc(assoc);
    info->total_size = (size_t)size << 10;

    size_t line_num  = info->total_size / info->line_size;

    if (0 == info->ways) {
        // full associative
        info->sets = 1;
        info->ways = line_num;
    } else {
        info->sets = line_num / info->ways;
    }

    return 0;
}

// 返回 1 表示信息无效，需要使用 cpuid(0x8000_001d) 获取缓存信息
static INIT_TEXT int parse_amd_l3(uint32_t reg, cache_info_t *info) {
    size_t line_size     =  reg        & 0xff;
    size_t lines_per_tag = (reg >>  8) & 0x0f;
    size_t assoc         = (reg >> 12) & 0x0f;
    size_t size          = (reg >> 18) & 0x3fff;
    size_t tag_size      = line_size * lines_per_tag;

    if ((9 == assoc) || (0 == tag_size)) {
        return 1;
    }

    info->line_size = tag_size;
    info->ways = parse_l2l3_assoc(assoc);
    info->total_size = (size_t)size << 19;

    size_t line_num  = info->total_size / info->line_size;

    if (0 == info->ways) {
        // full associative
        info->sets = 1;
        info->ways = line_num;
    } else {
        info->sets = line_num / info->ways;
    }

    return 0;
}

static INIT_TEXT void get_cache_info_amd() {
    uint32_t ebx;
    uint32_t ecx;
    uint32_t edx;

    __asm__("cpuid" : "=c"(ecx), "=d"(edx) : "a"(0x80000005) : "ebx");
    parse_amd_l1(ecx, &g_l1d_info);
    parse_amd_l1(edx, &g_l1i_info);

    __asm__("cpuid" : "=c"(ecx), "=d"(edx) : "a"(0x80000006) : "ebx");
    int l2_bad = parse_amd_l2(ecx, &g_l2_info);
    int l3_bad = parse_amd_l3(edx, &g_l3_info);

    if (l2_bad) {
        __asm__("cpuid" : "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(0x8000001d), "c"(2));
        int line_size = (ebx & 0xfff) + 1;
        int ways = ((ebx >> 22) & 0x3ff) + 1;
        dbg_print("L2 line_size=%d, %d-way\n", line_size, ways);
    }

    if (l3_bad) {
        __asm__("cpuid" : "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(0x8000001d), "c"(3));
        int line_size = (ebx & 0xfff) + 1;
        int ways = ((ebx >> 22) & 0x3ff) + 1;
        dbg_print("L3 line_size=%d, %d-way\n", line_size, ways);
    }
}



//------------------------------------------------------------------------------
// 获取缓存信息 -- Intel
//------------------------------------------------------------------------------

// 缓存信息分组记录：L1I、L1D、L2、L3
typedef enum cache_type {
    INVALID = 0,
    TLB_INST,
    TLB_DATA,
    L1_INST,
    L1_DATA,
    L2,
    L3,
} cache_type_t;

// 解析 CPUID leaf 2 返回的字节
static INIT_TEXT void parse_leaf_2_byte(uint8_t byte) {
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
    case 0x06: type = L1_INST; size =  8 * K; way = 4; line = 32; break;
    case 0x08: type = L1_INST; size = 16 * K; way = 4; line = 32; break;
    case 0x09: type = L1_INST; size = 32 * K; way = 4; line = 64; break;
    case 0x30: type = L1_INST; size = 32 * K; way = 8; line = 64; break;

    // L1 数据缓存
    case 0x0a: type = L1_DATA; size =  8 * K; way = 2; line = 32; break;
    case 0x0c: type = L1_DATA; size = 16 * K; way = 4; line = 32; break;
    case 0x0d: type = L1_DATA; size = 16 * K; way = 4; line = 64; break;
    case 0x0e: type = L1_DATA; size = 24 * K; way = 6; line = 64; break;
    case 0x2c: type = L1_DATA; size = 32 * K; way = 8; line = 64; break;
    case 0x60: type = L1_DATA; size = 16 * K; way = 8; line = 64; break;
    case 0x66: type = L1_DATA; size =  8 * K; way = 4; line = 64; break;
    case 0x67: type = L1_DATA; size = 16 * K; way = 4; line = 64; break;
    case 0x68: type = L1_DATA; size = 32 * K; way = 4; line = 64; break;

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
        type = ((0x0f == cpu_family) && (0x06 == cpu_model)) ? L3 : L2;
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
    case L1_INST: g_l1i_info = info; break;
    case L1_DATA: g_l1d_info = info; break;
    case L2: g_l2_info = info; break;
    case L3: g_l3_info = info; break;
    default: break;
    }
}

// 解析 CPUID leaf 2 寄存器
static INIT_TEXT void parse_leaf_2_reg(uint32_t reg) {
    if (0x80000000 & reg) {
        return;
    }

    parse_leaf_2_byte(reg & 0xff);
    reg >>= 8;
    parse_leaf_2_byte(reg & 0xff);
    reg >>= 8;
    parse_leaf_2_byte(reg & 0xff);
    reg >>= 8;
    parse_leaf_2_byte(reg & 0xff);
}

// AMD 不支持 leaf 4，需要用别的办法
// 例如 CPUID 8000_0005 和 8000_0006

// 需要判断CPU属于Intel还是AMD，在Linux中，该信息记录在boot_cpu_data结构体中
// 结构体在early_cpu_init函数里填充
// arch/x86/kernel/cpu/common.c: get_cpu_vendor

// 解析 CPUID leaf 4 调用结果
// 返回 1 表示 subleaf 无效，返回 0 表示 subleaf 有效
static INIT_TEXT int parse_leaf_4_res(uint32_t eax, uint32_t ebx, uint32_t ecx) {
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
static INIT_TEXT int parse_leaf_18_res(uint32_t ebx, uint32_t ecx, uint32_t edx) {
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

    dbg_print("level %d %s cache, %d-way, %d sets\n",
            level, type_name, ways, sets);

    return 0;
}


// 获取缓存信息的主函数
static INIT_TEXT void get_cache_info_intel() {
    uint32_t eax;
    uint32_t ebx;
    uint32_t ecx;
    uint32_t edx;

    // 调用 CPUID leaf 2
    __asm__("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(2));
    eax &= ~0xffU;
    parse_leaf_2_reg(eax);
    parse_leaf_2_reg(ebx);
    parse_leaf_2_reg(ecx);
    parse_leaf_2_reg(edx);

    // 通过 CPUID leaf 4 获取缓存参数
    // 新的 CPU 普遍通过这个 leaf 返回缓存信息
    // 循环遍历所有 subleaf（通过 ecx 传入）
    for (int n = 0; ; ++n) {
        __asm__("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx) : "a"(4), "c"(n));
        if (parse_leaf_4_res(eax, ebx, ecx)) {
            break;
        }
    }

    // 通过 CPUID leaf 18 获取 TLB 参数
    // 循环遍历所有 subleaf（通过 ecx 传入）
    uint32_t max_subleaf = 0xffffffff;
    for (uint32_t n = 0; n <= max_subleaf; ++n) {
        __asm__("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(0x18), "c"(n));
        if (0 == n) {
            max_subleaf = eax;
            // dbg_print("max subleaf for cpuid.18 is %d\n", max_subleaf);
        }
        if (parse_leaf_18_res(ebx, ecx, edx)) {
            break;
        }
    }
}


//------------------------------------------------------------------------------
// 获取总线频率，对应 APIC Timer 的基频
//------------------------------------------------------------------------------

CONST int64_t g_bus_freq = 0;
CONST int64_t g_tsc_freq = 0;

static INIT_TEXT void get_clock() {
    uint32_t eax;
    uint32_t ebx;
    uint32_t ecx;
    __asm__("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx) : "a"(0x15) : "edx");

    if (0 != ecx) {
        g_bus_freq = (int64_t)ecx; // 晶振频率
        if (0 != eax) {
            g_tsc_freq = (int64_t)ecx * ebx / eax;
        }
    }
}


//------------------------------------------------------------------------------
// 获取所有信息
//------------------------------------------------------------------------------

INIT_TEXT void get_cpu_info() {
    get_basic_info();

    if (0 == memcmp(g_cpu_vendor, VENDOR_INTEL, 12)) {
        get_cache_info_intel();
    } else if (0 == memcmp(g_cpu_vendor, VENDOR_AMD, 12)) {
        get_cache_info_amd();
    } else {
        dbg_print("unknown vendor name '%.12s'\n", g_cpu_vendor);
    }

    get_clock();
}
