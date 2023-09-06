// 使用 cpuid 检测支持的特性，设置 MSR 开启相关功能

#include <arch_cpu.h>
#include <liba/rw.h>
#include <debug.h>
#include <libk_string.h>



#define VENDOR_INTEL "GenuineIntel"
#define VENDOR_AMD   "AuthenticAMD"

static CONST char g_cpu_vendor[12];
static CONST char g_cpu_brand[48];

static CONST uint8_t  g_cpu_stepping;
static CONST uint8_t  g_cpu_model;
static CONST uint8_t  g_cpu_family;
static CONST uint8_t  g_cpu_type;
static CONST uint8_t  g_cpu_ex_model;
static CONST uint8_t  g_cpu_ex_family;

static CONST uint32_t g_cpu_features;

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

    __asm__("cpuid" : "=c"(c), "=d"(d) : "a"(0x80000005) : "ebx");
    amd_parse_l1(c, &g_l1d_info);
    amd_parse_l1(d, &g_l1i_info);

    __asm__("cpuid" : "=c"(c), "=d"(d) : "a"(0x80000006) : "ebx");
    int l2_bad = amd_parse_l2(c, &g_l2_info);
    int l3_bad = amd_parse_l3(d, &g_l3_info);

    if (l2_bad) {
        __asm__("cpuid" : "=b"(b), "=c"(c) : "a"(0x8000001d), "c"(2) : "edx");
        g_l2_info.line_size  = (b & 0xfff) + 1;
        g_l2_info.sets       = (size_t)c + 1;
        g_l2_info.ways       = ((b >> 22) & 0x3ff) + 1;
        g_l2_info.total_size = g_l2_info.line_size * g_l2_info.sets * g_l2_info.ways;
    }

    if (l3_bad) {
        __asm__("cpuid" : "=b"(b), "=c"(c) : "a"(0x8000001d), "c"(3) : "edx");
        g_l3_info.line_size  = (b & 0xfff) + 1;
        g_l3_info.sets       = (size_t)c + 1;
        g_l3_info.ways       = ((b >> 22) & 0x3ff) + 1;
        g_l3_info.total_size = g_l3_info.line_size * g_l3_info.sets * g_l3_info.ways;
    }
}


//------------------------------------------------------------------------------
// Intel 获取缓存信息
//------------------------------------------------------------------------------

static INIT_TEXT void intel_get_cache_info() {
    //
}


//------------------------------------------------------------------------------
// 公开函数
//------------------------------------------------------------------------------

INIT_TEXT void cpu_info_detect() {
    uint32_t a, b, c, d;

    __asm__("cpuid" : "=b"(b), "=c"(c), "=d"(d) : "a"(0));
    kmemcpy(g_cpu_vendor, (uint32_t[]){ b,d,c }, 12);
    __asm__("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(0x80000002));
    kmemcpy(g_cpu_brand, (uint32_t[]){ a,b,c,d }, 16);
    __asm__("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(0x80000003));
    kmemcpy(&g_cpu_brand[16], (uint32_t[]){ a,b,c,d }, 16);
    __asm__("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(0x80000004));
    kmemcpy(&g_cpu_brand[32], (uint32_t[]){ a,b,c,d }, 16);

    __asm__("cpuid" : "=a"(a), "=c"(c), "=d"(d) : "a"(1) : "ebx");
    g_cpu_stepping  =  a        & 0x0f;
    g_cpu_model     = (a >>  4) & 0x0f;
    g_cpu_family    = (a >>  8) & 0x0f;
    g_cpu_type      = (a >> 12) & 0x03;
    g_cpu_ex_model  = (a >> 16) & 0x0f;
    g_cpu_ex_family = (a >> 20) & 0xff;
    g_cpu_features  = 0;
    g_cpu_features |= (c & (1U << 17)) ? CPU_FEATURE_PCID   : 0;
    g_cpu_features |= (c & (1U << 21)) ? CPU_FEATURE_X2APIC : 0;
    g_cpu_features |= (d & (1U <<  4)) ? CPU_FEATURE_TSC    : 0;

    __asm__("cpuid" : "=b"(b) : "a"(7), "c"(0) : "edx");
    g_cpu_features |= (b & (1U <<  9)) ? CPU_FEATURE_ERMS    : 0;
    g_cpu_features |= (b & (1U << 10)) ? CPU_FEATURE_INVPCID : 0;

    __asm__("cpuid" : "=d"(d) : "a"(0x80000001) : "ebx", "ecx");
    g_cpu_features |= (d & (1U << 20)) ? CPU_FEATURE_NX : 0;
    g_cpu_features |= (d & (1U << 26)) ? CPU_FEATURE_1G : 0;

    __asm__("cpuid" : "=a"(a) : "a"(6) : "ebx", "ecx", "edx");
    g_cpu_features |= (a & (1U << 2)) ? CPU_FEATURE_APIC_CONSTANT : 0;

    if (0 == kmemcmp(g_cpu_vendor, VENDOR_INTEL, 12)) {
        intel_get_cache_info();
    } else if (0 == kmemcmp(g_cpu_vendor, VENDOR_AMD, 12)) {
        amd_get_cache_info();
    } else {
        dbg_print("unknown vendor name '%.12s'\n", g_cpu_vendor);
    }
}

INIT_TEXT void cpu_features_init() {
    if (CPU_FEATURE_NX & g_cpu_features) {
        uint64_t efer = read_msr(MSR_EFER);
        efer |= 1UL << 11;  // NXE
        write_msr(MSR_EFER, efer);
    }

    uint64_t cr0 = read_cr0();
    cr0 |=  (1UL << 16); // WP 分页写保护
    write_cr0(cr0);

    uint64_t cr4 = read_cr4();
    cr4 |= 1UL << 2; // time stamp counter
    cr4 |= 1UL << 7; // PGE 全局页（不会从 TLB 中清除）
    if (CPU_FEATURE_PCID & g_cpu_features) {
        cr4 |= 1UL << 17; // PCIDE 上下文标识符
    }
    write_cr4(cr4);
}

#ifdef DEBUG

INIT_TEXT void cpu_info_show() {
    dbg_print("cpu vendor: '%12s'\n", g_cpu_vendor);
    dbg_print("cpu brand: '%48s'\n", g_cpu_brand);

    struct {
        const char *name;
        uint32_t mask;
    } feats[] = {
        { "pcid",    CPU_FEATURE_PCID          },
        { "x2apic",  CPU_FEATURE_X2APIC        },
        { "tsc",     CPU_FEATURE_TSC           },
        { "nx",      CPU_FEATURE_NX            },
        { "pdpe1gb", CPU_FEATURE_1G            },
        { "fixfreq", CPU_FEATURE_APIC_CONSTANT },
        { "incpcid", CPU_FEATURE_INVPCID       }
    };
    size_t nfeats = sizeof(feats) / sizeof(feats[0]);

    dbg_print("cpu flags:");
    for (size_t i = 0; i < nfeats; ++i) {
        if (g_cpu_features & feats[i].mask) {
            dbg_print(" %s", feats[i].name);
        }
    }
    dbg_print("\n");
}

#endif
