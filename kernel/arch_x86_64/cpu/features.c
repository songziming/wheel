// 检查 CPU 支持哪些功能，控制这些功能的启用

#include "features.h"
#include <arch_api.h>
#include <acpi/acpi.h>
#include <kstring.h>
#include <debug.h>

#include <console.h>
#include <kshell.h>


#define VENDOR_INTEL "GenuineIntel"
#define VENDOR_AMD   "AuthenticAMD"


static CONST uint32_t g_vendor[3];
static CONST uint8_t  g_cpu_model;
static CONST uint8_t g_cpu_family;

CONST uint32_t g_cpu_features;

cache_info_t g_l1d_info;
cache_info_t g_l1i_info;
cache_info_t g_l2_info;
cache_info_t g_l3_info;

static uint32_t g_core_freq;
static uint32_t g_tsc_clk[2];

static uint32_t g_base_freq;
static uint32_t g_max_freq;
static uint32_t g_bus_freq;


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
        info->sets = 1;
        info->ways = line_num;
    } else {
        info->sets = line_num / info->ways;
    }
}

static INIT_TEXT size_t amd_l2l3_assoc(size_t assoc) {
    switch (assoc) {
    case  5: return     6;
    case  6: return     8;
    case  8: return    16;
    case 10: return    32;
    case 11: return    48;
    case 12: return    64;
    case 13: return    96;
    case 14: return   128;
    case 15: return     0;
    default: return assoc;
    }
}

// 返回 1 表示缓存信息无效，需要用 cpuid(0x8000001d)
static INIT_TEXT int amd_parse_l2(uint32_t reg, cache_info_t *info) {
    size_t line_size     =  reg        & 0xff;
    size_t lines_per_tag = (reg >>  8) & 0x0f;
    size_t assoc         = (reg >> 12) & 0x0f;
    size_t size          = (reg >> 16) & 0xffff;
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
    size_t size          = (reg >> 18) & 0x3fff;
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

    ASMV("cpuid" : "=c"(c), "=d"(d) : "a"(0x80000005) : "ebx");
    amd_parse_l1(c, &g_l1d_info);
    amd_parse_l1(d, &g_l1i_info);

    ASMV("cpuid" : "=c"(c), "=d"(d) : "a"(0x80000006) : "ebx");
    int l2_bad = amd_parse_l2(c, &g_l2_info);
    int l3_bad = amd_parse_l3(d, &g_l3_info);

    if (l2_bad) {
        ASMV("cpuid" : "=b"(b), "=c"(c) : "a"(0x8000001d), "c"(2) : "edx");
        g_l2_info.line_size  = (b & 0xfff) + 1;
        g_l2_info.sets       = (size_t)c + 1;
        g_l2_info.ways       = ((b >> 22) & 0x3ff) + 1;
        g_l2_info.total_size = g_l2_info.line_size * g_l2_info.sets * g_l2_info.ways;
    }

    if (l3_bad) {
        ASMV("cpuid" : "=b"(b), "=c"(c) : "a"(0x8000001d), "c"(3) : "edx");
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
    case 0x49:
        type = ((0x0f == g_cpu_family) && (0x06 == g_cpu_model)) ? L3 : L2;
        size = 4 * M; way = 16; line = 64;
        break;

    // 预取缓存
    case 0xf0:
    case 0xf1:
        break;

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
// 返回 1 表示 subleaf 无效
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

static INIT_TEXT void intel_get_cache_info() {
    uint32_t a, b, c, d;

    // 调用 CPUID leaf 2
    ASMV("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(2));
    a &= ~0xffU;
    intel_parse_leaf2(a);
    intel_parse_leaf2(b);
    intel_parse_leaf2(c);
    intel_parse_leaf2(d);

    // 通过 CPUID leaf 4 获取缓存参数
    // 新的 CPU 普遍通过这个 leaf 返回缓存信息
    for (int n = 0; ; ++n) {
        ASMV("cpuid" : "=a"(a), "=b"(b), "=c"(c) : "a"(4), "c"(n));
        if (intel_parse_leaf4(a, b, c)) {
            break;
        }
    }
}


//------------------------------------------------------------------------------
// Intel 获取 CPU 拓扑结构
//------------------------------------------------------------------------------

static INIT_TEXT void intel_get_topology() {
    uint32_t a, b, c, d;

    // 遍历子 leaf 获取各级拓扑信息
    for (int domain = 0; ; ++domain) {
        ASMV("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(0x1f), "c"(domain));
        int type = (c >> 8) & 0xff;
        if (0 == type) {
            break;
        }
        logk("topology level %d, x2APIC ID shift %d, %d logical processors\n",
            type, a & 0x1f, b & 0xffff);
    }
}


//------------------------------------------------------------------------------
// Intel 检测 VT-d（I/O 虚拟化）
//------------------------------------------------------------------------------

static INIT_TEXT void intel_detect_vtd() {
    // VT-d 的前提是 VMX 已存在
    if (!(CPU_FEATURE_VMX & g_cpu_features)) {
        return;
    }

    // VT-d 由 ACPI DMAR 表宣告，存在 DMAR 表即表示支持
    // TODO 运行此代码时，acpi 尚未解析，暂时不能获取 DMAR 表
    // if (acpi_table_find("DMAR", 0)) {
    //     g_cpu_features |= CPU_FEATURE_VTD;
    // }
}


//------------------------------------------------------------------------------
// AMD 检测虚拟化支持（SVM）
//------------------------------------------------------------------------------

static INIT_TEXT void amd_detect_svm() {
    uint32_t c;
    ASMV("cpuid" : "=c"(c) : "a"(0x80000001) : "ebx", "edx");
    g_cpu_features |= (c & (1U << 2)) ? CPU_FEATURE_SVM : 0;
}


//------------------------------------------------------------------------------
// 获取处理器信息
//------------------------------------------------------------------------------

// 获取处理器功能开关
// 参考 linux/arch/x86/boot/cpuflags.c, 函数 get_cpuflags(void)
INIT_TEXT void cpu_features_detect() {
    uint32_t a, b, c, d;
    uint32_t g_max_eax;

    // 获取 vendor string
    ASMV("cpuid" : "=a"(g_max_eax), "=b"(g_vendor[0]), "=c"(g_vendor[2]), "=d"(g_vendor[1]) : "a"(0));

    // basic information
    g_cpu_features  = 0;
    ASMV("cpuid" : "=a"(a), "=c"(c), "=d"(d) : "a"(1) : "ebx");
    // g_cpu_stepping  =  a        & 0x0f;
    g_cpu_model     = (a >>  4) & 0x0f;
    g_cpu_family    = (a >>  8) & 0x0f;
    // g_cpu_type      = (a >> 12) & 0x03;
    // g_cpu_ex_model  = (a >> 16) & 0x0f;
    // g_cpu_ex_family = (a >> 20) & 0xff;
    g_cpu_features |= (c & (1U <<  5)) ? CPU_FEATURE_VMX     : 0;
    g_cpu_features |= (c & (1U << 17)) ? CPU_FEATURE_PCID    : 0;
    g_cpu_features |= (c & (1U << 21)) ? CPU_FEATURE_X2APIC  : 0;
    g_cpu_features |= (c & (1U << 24)) ? CPU_FEATURE_TSC_DDL : 0;
    g_cpu_features |= (d & (1U <<  4)) ? CPU_FEATURE_TSC     : 0;
    // g_cpu_features |= (d & (1U << 13)) ? CPU_FEATURE_PGE     : 0;
    // g_cpu_features |= (d & (1U << 16)) ? CPU_FEATURE_PAT     : 0;
    g_cpu_features |= (d & (1U << 28)) ? CPU_FEATURE_HT      : 0;
    // if (g_cpu_features & CPU_FEATURE_HT) {
    //     g_num_ids = (b >> 16) & 0xff;
    // }

    // extended signature and feature
    ASMV("cpuid" : "=d"(d) : "a"(0x80000001) : "ebx", "ecx");
    g_cpu_features |= (d & (1U << 20)) ? CPU_FEATURE_NX : 0;
    g_cpu_features |= (d & (1U << 26)) ? CPU_FEATURE_1G : 0;

    // extended info 1
    ASMV("cpuid" : "=d"(d) : "a"(0x80000001) : "ebx", "ecx");
    g_cpu_features |= (d & (1U << 8)) ? CPU_FEATURE_TSC_FIXED : 0;

    // thermal and power
    ASMV("cpuid" : "=a"(a) : "a"(6) : "ebx", "ecx", "edx");
    g_cpu_features |= (a & (1U <<  2)) ? CPU_FEATURE_ARAT     : 0;
    g_cpu_features |= (a & (1U << 19)) ? CPU_FEATURE_FEEDBACK : 0;

    // structured extended feature, main sub-leaf
    ASMV("cpuid" : "=b"(b) : "a"(7), "c"(0) : "edx");
    g_cpu_features |= (b & (1U << 1)) ? CPU_FEATURE_TSC_ADJUST : 0;

    // get core crystal's frequency
    // TSC 频率也是这个
    ASMV("cpuid" : "=a"(a), "=b"(b), "=c"(c) : "a"(0x15) : "edx");
    g_core_freq = c; // in Hz (Always Running Timer value, or ART-value)
    g_tsc_clk[0] = b;
    g_tsc_clk[1] = a;
    // tsc_freq = g_core_freq * b / a

    // get bus frequency
    // 486 Local APIC 是外置的，和处理器主频无关，而是和总线频率绑定
    ASMV("cpuid" : "=a"(a), "=b"(b), "=c"(c) : "a"(0x16) : "edx");
    g_base_freq = a;
    g_max_freq = b;
    g_bus_freq = c; // in MHz

    // 获取各级缓存信息，获取方式与 vendor 有关
    if (0 == kmemcmp(g_vendor, VENDOR_INTEL, 12)) {
        intel_get_cache_info();
        intel_get_topology();
        intel_detect_vtd();
    } else if (0 == kmemcmp(g_vendor, VENDOR_AMD, 12)) {
        amd_get_cache_info();
        amd_detect_svm();
    } else {
        logk("unknown vendor name '%.12s'\n", (char*)g_vendor);
    }
}

// 开启功能开关
INIT_TEXT void cpu_features_enable() {
    // 设置 cr0
    uint64_t cr0 = read_cr0();
    cr0 |=  (1UL << 16); // WP 分页写保护
    write_cr0(cr0);

    // 设置 cr4
    uint64_t cr4 = read_cr4();
    cr4 |= 1UL << 2; // time stamp counter
    cr4 |= 1UL << 5; // PAE（应该已经开启了）
    cr4 |= 1UL << 7; // PGE 全局页（标记为 global 的页表项不会从 TLB 中清除）
    if (CPU_FEATURE_FSGSBASE & g_cpu_features) {
        cr4 |= 1UL << 16; // FSGSBASE 启用读写 fs.base、gs.base 的指令
    }
    if (CPU_FEATURE_PCID & g_cpu_features) {
        cr4 |= 1UL << 17; // PCIDE 上下文标识符
    }
    if (CPU_FEATURE_SMEP & g_cpu_features) {
        cr4 |= 1UL << 20; // SMEP
    }
    if (CPU_FEATURE_SMAP & g_cpu_features) {
        cr4 |= 1UL << 21; // SMAP
    }
    write_cr4(cr4);

    // 设置 PAT，使用编号 4 表示 Write-Combined
    uint64_t pat = read_msr(MSR_PAT);
    pat &= ~(7UL << 16);
    pat |= 0x01UL << 16;    // Write-Combined (0x01)
    write_msr(MSR_PAT, pat);

    // 设置 efer
    uint64_t efer = read_msr(MSR_EFER);
    efer |= (1UL <<  0); // SCE，启用快速系统调用指令 syscall/sysret
    if (CPU_FEATURE_NX & g_cpu_features) {
        efer |= 1UL << 11;  // NXE
    }
    write_msr(MSR_EFER, efer);
}

// arch-api func
size_t arch_cacheline_size() {
    return g_l1d_info.line_size;
}

//------------------------------------------------------------------------------
// 调试命令
//------------------------------------------------------------------------------

#if !defined(UNIT_TEST)

static void cpu_features_show() {
    console_printf("cpu info:\n");
    console_printf("  - vendor: %.12s\n", (char*)g_vendor);

    console_printf("  - L1I line=%zu, nsets=%zu, nways=%zu, ncolors=%zu\n",
        g_l1i_info.line_size, g_l1i_info.sets, g_l1i_info.ways,
        g_l1i_info.line_size * g_l1i_info.sets >> PAGE_SHIFT);
    console_printf("  - L1D line=%zu, nsets=%zu, nways=%zu, ncolors=%zu\n",
        g_l1d_info.line_size, g_l1d_info.sets, g_l1d_info.ways,
        g_l1d_info.line_size * g_l1d_info.sets >> PAGE_SHIFT);
    console_printf("  - L2  line=%zu, nsets=%zu, nways=%zu, ncolors=%zu\n",
        g_l2_info.line_size, g_l2_info.sets, g_l2_info.ways,
        g_l2_info.line_size * g_l2_info.sets >> PAGE_SHIFT);
    console_printf("  - L3  line=%zu, nsets=%zu, nways=%zu, ncolors=%zu\n",
        g_l3_info.line_size, g_l3_info.sets, g_l3_info.ways,
        g_l3_info.line_size * g_l3_info.sets >> PAGE_SHIFT);

    static const struct {
        const char *name;
        uint32_t mask;
    } FEATS[] = {
        { "pcid",       CPU_FEATURE_PCID       },
        { "x2apic",     CPU_FEATURE_X2APIC     },
        { "tsc",        CPU_FEATURE_TSC        },
        { "ht",         CPU_FEATURE_HT         },
        { "nx",         CPU_FEATURE_NX         },
        { "1g",         CPU_FEATURE_1G         },
        { "arat",       CPU_FEATURE_ARAT       },
        { "incpcid",    CPU_FEATURE_INVPCID    },
        { "smep",       CPU_FEATURE_SMEP       },
        { "smap",       CPU_FEATURE_SMAP       },
        { "fsgsbase",   CPU_FEATURE_FSGSBASE   },
        { "feedback",   CPU_FEATURE_FEEDBACK   },
        { "vmx",        CPU_FEATURE_VMX        },
        { "svm",        CPU_FEATURE_SVM        },
        { "vtd",        CPU_FEATURE_VTD        },
        { "tsc-fixed",  CPU_FEATURE_TSC_FIXED  },
        { "tsc-adjust", CPU_FEATURE_TSC_ADJUST },
        { "tsc-ddl",    CPU_FEATURE_TSC_DDL    },
    };
    size_t nfeats = sizeof(FEATS) / sizeof(FEATS[0]);

    console_printf("  - features:");
    for (size_t i = 0; i < nfeats; ++i) {
        if (g_cpu_features & FEATS[i].mask) {
            console_printf(" %s", FEATS[i].name);
        }
    }
    console_printf("\n");
    console_printf("  - core-freq: %dHz, tsc/clock=%d/%d, base-freq: %dMHz, max-freq: %dMHz, bus-freq: %dMHz\n",
        g_core_freq, g_tsc_clk[0], g_tsc_clk[1], g_base_freq, g_max_freq, g_bus_freq);
}

KSHELL_CMD("cpu", cpu_features_show);

#endif // !defined(UNIT_TEST)
