#include <arch_smp.h>
#include <debug.h>
#include <liba/rw.h>
#include <arch_cpu.h>


#define SHOW_MADT_CONTENT 1

#if defined(DEBUG) && SHOW_MADT_CONTENT
    #define MADT_PRINT(...) klog(__VA_ARGS__)
#else
    #define MADT_PRINT(...)
#endif


typedef struct loapic {
    uint32_t apic_id;
    uint32_t processor_id;
    uint32_t flags;
} loapic_t;

typedef struct ioapic {
    uint32_t apic_id;
    uint32_t gsi_base;
    size_t   address;   // 映射的虚拟地址
} ioapic_t;


static CONST size_t    g_loapic_addr;
       CONST int       g_loapic_num = 0;
       CONST int       g_ioapic_num = 0;
static CONST loapic_t *g_loapics    = NULL;
static CONST ioapic_t *g_ioapics    = NULL;


// IRQ 与 GSI 的映射表
// 这里的 irq_max、gsi_max 指的是映射表条目数量，并非中断向量的个数
static CONST uint8_t   g_irq_max    = 0;
static CONST uint32_t  g_gsi_max    = 0;
static CONST uint32_t *g_irq_to_gsi = NULL;
static CONST uint8_t  *g_gsi_to_irq = NULL;
static CONST uint16_t *g_gsi_flags  = NULL;


// 解析 MADT，记录 Local APIC、IO APIC
INIT_TEXT void parse_madt(madt_t *madt) {
    ASSERT(0 == g_loapic_num);
    ASSERT(0 == g_ioapic_num);
    ASSERT(NULL == g_loapics);
    ASSERT(NULL == g_ioapics);
    ASSERT(NULL == g_irq_to_gsi);
    ASSERT(NULL == g_gsi_to_irq);
    ASSERT(NULL == g_gsi_flags);

#if defined(DEBUG) && SHOW_MADT_CONTENT
    if (1 & madt->flags) {
        klog("8259 vectors must be disabled\n");
    }
#endif

    const size_t LEN = madt->header.length;

    g_loapic_addr = madt->loapic_addr;

    // 第一次遍历 MADT，统计 Local APIC、IO APIC 的数量、irq-gsi 映射表大小
    for (size_t i = sizeof(madt_t); i < LEN;) {
        acpi_subtbl_t *sub = (acpi_subtbl_t *)((size_t)madt + i);
        i += sub->length;

        switch (sub->type) {
        case MADT_TYPE_LOCAL_APIC_OVERRIDE:
            g_loapic_addr = ((madt_loapic_override_t *)sub)->address;
            break;
        case MADT_TYPE_LOCAL_APIC: {
            madt_loapic_t *lo = (madt_loapic_t *)sub;
            if (1 & lo->loapic_flags) {
                ++g_loapic_num;
            }
            break;
        }
        case MADT_TYPE_LOCAL_X2APIC: {
            madt_lox2apic_t *lo = (madt_lox2apic_t *)sub;
            if (1 & lo->loapic_flags) {
                ++g_loapic_num;
            }
            break;
        }
        case MADT_TYPE_IO_APIC:
            ++g_ioapic_num;
            break;
        case MADT_TYPE_INTERRUPT_OVERRIDE: {
            madt_int_override_t *override = (madt_int_override_t *)sub;
            if (override->source > g_irq_max) {
                g_irq_max = override->source;
            }
            if (override->gsi > g_gsi_max) {
                g_gsi_max = override->gsi;
            }
            break;
        }
        case MADT_TYPE_NMI_SOURCE: {
            madt_nmi_t *nmi = (madt_nmi_t *)sub;
            if (nmi->gsi > g_gsi_max) {
                g_gsi_max = nmi->gsi;
            }
            break;
        }
        default:
            break;
        }
    }

    // 为 Local APIC、IO APIC 和中断向量映射表设备分配空间
    g_loapics    = early_alloc_ro(g_loapic_num * sizeof(loapic_t));
    g_ioapics    = early_alloc_ro(g_ioapic_num * sizeof(ioapic_t));
    g_irq_to_gsi = early_alloc_ro((g_irq_max + 1) * sizeof(uint32_t));
    g_gsi_to_irq = early_alloc_ro((g_gsi_max + 1) * sizeof(uint8_t));
    g_gsi_flags  = early_alloc_ro((g_gsi_max + 1) * sizeof(uint16_t));

    // 默认情况下，8259 IRQ 0~15 与 GSI 0~15 对应
    for (uint8_t i = 0; i < g_irq_max; ++i) {
        g_irq_to_gsi[i] = i;
    }
    for (uint32_t i = 0; (i < g_gsi_max) && (i <= UINT8_MAX); ++i) {
        g_gsi_to_irq[i] = i;
    }

    // TODO 再次遍历 MADT，创建设备
    int loapic_idx = 0;
    int ioapic_idx = 0;
    MADT_PRINT("MADT content:\n");
    for (size_t i = sizeof(madt_t); i < LEN;) {
        acpi_subtbl_t *sub = (acpi_subtbl_t *)((size_t)madt + i);
        i += sub->length;

        switch (sub->type) {
        case MADT_TYPE_LOCAL_APIC: {
            madt_loapic_t *lo = (madt_loapic_t *)sub;
            MADT_PRINT("  - LOCAL_APIC id=%d, proc-id=%d, flags=%x\n",
                lo->id, lo->processor_id, lo->loapic_flags);
            if (1 & lo->loapic_flags) {
                g_loapics[loapic_idx].apic_id      = lo->id;
                g_loapics[loapic_idx].processor_id = lo->processor_id;
                g_loapics[loapic_idx].flags        = lo->loapic_flags;
                ++loapic_idx;
            }
            break;
        }
        case MADT_TYPE_LOCAL_X2APIC: {
            madt_lox2apic_t *lo = (madt_lox2apic_t *)sub;
            MADT_PRINT("  - LOCAL_X2APIC id=%d, proc-id=%d, flags=%x\n",
                lo->id, lo->processor_id, lo->loapic_flags);
            if (1 & lo->loapic_flags) {
                g_loapics[loapic_idx].apic_id      = lo->id;
                g_loapics[loapic_idx].processor_id = lo->processor_id;
                g_loapics[loapic_idx].flags        = lo->loapic_flags;
                ++loapic_idx;
            }
            break;
        }
        case MADT_TYPE_IO_APIC: {
            madt_ioapic_t *io = (madt_ioapic_t *)sub;
            MADT_PRINT("  - IO_APIC id=%d, gsi-base=%d, addr=%x\n",
                io->id, io->gsi_base, io->address);
            g_ioapics[ioapic_idx].apic_id  = io->id;
            g_ioapics[ioapic_idx].gsi_base = io->gsi_base;
            g_ioapics[ioapic_idx].address  = io->address;
            ++ioapic_idx;
            break;
        }
        case MADT_TYPE_INTERRUPT_OVERRIDE: {
            madt_int_override_t *override = (madt_int_override_t *)sub;
            MADT_PRINT("  - INT_OVERRIDE irq=%d, gsi=%u\n", override->source, override->gsi);
            g_irq_to_gsi[override->source] = override->gsi;
            g_gsi_to_irq[override->gsi] = override->source;
            g_gsi_flags [override->gsi] = override->inti_flags;
            break;
        }
        default:
            break;
        }
    }
    ASSERT(loapic_idx == g_loapic_num);
    ASSERT(ioapic_idx == g_ioapic_num);

    // TODO 第三次遍历 MADT，统计 NMI 信息
    //      标记各 APIC 的哪个引脚属于 NMI
    for (size_t i = sizeof(madt_t); i < LEN;) {
        acpi_subtbl_t *sub = (acpi_subtbl_t *)((size_t)madt + i);
        i += sub->length;

        switch (sub->type) {
        case MADT_TYPE_NMI_SOURCE: {
            madt_nmi_t *nmi = (madt_nmi_t *)sub;
            MADT_PRINT("  - NMI_SOURCE gsi=%u\n", nmi->gsi);
            break;
        }
        case MADT_TYPE_LOCAL_APIC_NMI: {
            madt_loapic_nmi_t *nmi = (madt_loapic_nmi_t *)sub;
            MADT_PRINT("  - LOCAL_APIC_NMI proc-id=%d, lint=%d\n",
                nmi->processor_id, nmi->lint);
            break;
        }
        case MADT_TYPE_LOCAL_X2APIC_NMI: {
            madt_lox2apic_nmi_t *nmi = (madt_lox2apic_nmi_t *)sub;
            MADT_PRINT("  - LOCAL_X2APIC_NMI proc-id=%d, lint=%d\n",
                nmi->processor_id, nmi->lint);
            break;
        }
        default:
            break;
        }
    }
}


//------------------------------------------------------------------------------
// PCPU 数据管理
//------------------------------------------------------------------------------

CONST size_t *g_pcpu_offsets = NULL; // 由 arch_mem.c 设置
static PCPU_BSS int g_cpu_index; // 当前 CPU 编号

INIT_TEXT void gsbase_init(int idx) {
    ASSERT(NULL != g_pcpu_offsets);
    ASSERT(idx < g_loapic_num);

    write_gsbase(g_pcpu_offsets[idx]);
    __asm__("movl %0, %%gs:%1" :: "r"(idx), "m"(g_cpu_index));
    // __asm__("movl %0, %%gs:(g_cpu_index)" :: "r"(idx));
}

inline int cpu_count() {
    return g_loapic_num;
}

inline int cpu_index() {
    int idx;
    __asm__("movl %%gs:%1, %0" : "=a"(idx) : "m"(g_cpu_index));
    // __asm__("movl %%gs:(g_cpu_index), %0" : "=a"(idx));
    return idx;
}

inline void *pcpu_ptr(int idx, void *ptr) {
    ASSERT(NULL != g_pcpu_offsets);
    ASSERT(idx < g_loapic_num);

    return (uint8_t *)ptr + g_pcpu_offsets[idx];
}

inline void *this_ptr(void *ptr) {
    ASSERT(NULL != g_pcpu_offsets);

    return (uint8_t *)ptr + read_gsbase();
}
