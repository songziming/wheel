// 检查 CPU 支持哪些功能，控制这些功能的启用

#include "features.h"
#include <arch_api.h>
#include <kstring.h>
#include <debug.h>


#define VENDOR_INTEL "GenuineIntel"
#define VENDOR_AMD   "AuthenticAMD"


static CONST uint32_t g_vendor[3];
static CONST uint8_t  g_cpu_model;
static CONST uint8_t g_cpu_family;

CONST uint32_t g_cpu_features;

static uint32_t g_core_freq;
static uint32_t g_tsc_clk[2];

static uint32_t g_base_freq;
static uint32_t g_max_freq;
static uint32_t g_bus_freq;


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
    g_cpu_features |= (c & (1U <<  5)) ? CPU_FEATURE_VMX    : 0;
    g_cpu_features |= (c & (1U << 17)) ? CPU_FEATURE_PCID   : 0;
    g_cpu_features |= (c & (1U << 21)) ? CPU_FEATURE_X2APIC : 0;
    g_cpu_features |= (d & (1U <<  4)) ? CPU_FEATURE_TSC    : 0;
    g_cpu_features |= (d & (1U << 28)) ? CPU_FEATURE_HT     : 0;
    // if (g_cpu_features & CPU_FEATURE_HT) {
    //     g_num_ids = (b >> 16) & 0xff;
    // }

    // extended function
    ASMV("cpuid" : "=d"(d) : "a"(0x80000001) : "ebx", "ecx");
    g_cpu_features |= (d & (1U << 20)) ? CPU_FEATURE_NX : 0;
    g_cpu_features |= (d & (1U << 26)) ? CPU_FEATURE_1G : 0;

    // thermal and power
    ASMV("cpuid" : "=a"(a) : "a"(6) : "ebx", "ecx", "edx");
    g_cpu_features |= (a & (1U <<  2)) ? CPU_FEATURE_ARAT     : 0;
    g_cpu_features |= (a & (1U << 19)) ? CPU_FEATURE_FEEDBACK : 0;

    // get core crystal's frequency
    // TSC 频率也是这个
    ASMV("cpuid" : "=a"(a), "=b"(b), "=c"(c) : "a"(0x15) : "edx");
    g_core_freq = c; // in Hz
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
        // intel_get_cache_info();
        // intel_get_topology();
        // intel_detect_vmx();
    } else if (0 == kmemcmp(g_vendor, VENDOR_AMD, 12)) {
        // amd_get_cache_info();
        // amd_detect_svm();
    } else {
        logk("unknown vendor name '%.12s'\n", g_vendor);
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

    // 设置 efer
    uint64_t efer = read_msr(MSR_EFER);
    efer |= (1UL <<  0); // SCE，启用快速系统调用指令 syscall/sysret
    if (CPU_FEATURE_NX & g_cpu_features) {
        efer |= 1UL << 11;  // NXE
    }
    write_msr(MSR_EFER, efer);
}

void cpu_features_show() {
    logk("cpu info:\n");
    logk("  - vendor: %.12s\n", (char*)g_vendor);

    static const struct {
        const char *name;
        uint32_t mask;
    } FEATS[] = {
        { "pcid",     CPU_FEATURE_PCID      },
        { "x2apic",   CPU_FEATURE_X2APIC    },
        { "tsc",      CPU_FEATURE_TSC       },
        { "ht",       CPU_FEATURE_HT        },
        { "nx",       CPU_FEATURE_NX        },
        { "pdpe1gb",  CPU_FEATURE_1G        },
        { "arat",     CPU_FEATURE_ARAT      },
        { "incpcid",  CPU_FEATURE_INVPCID   },
        { "smep",     CPU_FEATURE_SMEP      },
        { "smap",     CPU_FEATURE_SMAP      },
        { "fsgsbase", CPU_FEATURE_FSGSBASE  },
        { "feedback", CPU_FEATURE_FEEDBACK  },
        { "vmx",      CPU_FEATURE_VMX       },
        { "svm",      CPU_FEATURE_SVM       },
    };
    size_t nfeats = sizeof(FEATS) / sizeof(FEATS[0]);

    logk("  - features:");
    for (size_t i = 0; i < nfeats; ++i) {
        if (g_cpu_features & FEATS[i].mask) {
            logk(" %s", FEATS[i].name);
        }
    }
    logk("\n");
    logk("  - core-freq: %dHz, tsc/clock=%d/%d, base-freq: %dMHz, max-freq: %dMHz, bus-freq: %dMHz\n",
        g_core_freq, g_tsc_clk[0], g_tsc_clk[1], g_base_freq, g_max_freq, g_bus_freq);
}
