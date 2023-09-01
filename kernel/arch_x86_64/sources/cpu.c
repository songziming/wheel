// 使用 cpuid 检测支持的特性，设置 MSR 开启相关功能

#include <cpu.h>
#include <liba/rw.h>
#include <debug.h>



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




INIT_TEXT void get_cpu_info() {
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

    uint32_t a, b, c, d;
    g_cpu_features = 0;

    // 获取型号和功能特性
    __asm__("cpuid" : "=a"(a), "=c"(c), "=d"(d) : "a"(1) : "ebx");
    g_cpu_stepping  =  a        & 0x0f;
    g_cpu_model     = (a >>  4) & 0x0f;
    g_cpu_family    = (a >>  8) & 0x0f;
    g_cpu_type      = (a >> 12) & 0x03;
    g_cpu_ex_model  = (a >> 16) & 0x0f;
    g_cpu_ex_family = (a >> 20) & 0xff;
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
}


INIT_TEXT void cpu_feat_init() {
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

INIT_TEXT void cpu_features_show() {
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

    dbg_print("cpu features:");
    for (size_t i = 0; i < nfeats; ++i) {
        if (g_cpu_features & feats[i].mask) {
            dbg_print(" %s", feats[i].name);
        }
    }
    dbg_print("\n");
}

#endif
