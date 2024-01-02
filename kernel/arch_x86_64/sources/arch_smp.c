#include <arch_smp.h>
#include <wheel.h>



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


CONST size_t g_loapic_addr;
CONST int    g_loapic_num = 0;
CONST int    g_ioapic_num = 0;

static CONST loapic_t *g_loapics = NULL;
static CONST ioapic_t *g_ioapics = NULL;

// IRQ 与 GSI 的映射表
// 这里的 irq_max、gsi_max 指的是映射表条目数量，并非中断向量的个数
static CONST uint8_t   g_irq_max;
static CONST uint32_t  g_gsi_max;
static CONST uint32_t *g_irq_to_gsi = NULL;
static CONST uint8_t  *g_gsi_to_irq = NULL;
static CONST uint16_t *g_gsi_flags  = NULL;

// NMI 连接到哪个 IO APIC
static CONST uint32_t g_nmi_gsi = UINT32_MAX; // -1 表示不经过 IO APIC，直接连接 Local APIC

// NMI 直接连接到哪个处理器的哪个 LINT 引脚
static CONST uint32_t g_nmi_cpu = UINT32_MAX; // -1 表示连接到所有处理器
static CONST uint8_t  g_nmi_lint = 0;
static CONST uint8_t  g_nmi_inti = 0;   // 触发模式（edge/level、low/high）



inline int cpu_count() {
    ASSERT(0 != g_loapic_num);
    return g_loapic_num;
}

// 判断 NMI 连接到指定 CPU 的哪个 LINT 引脚，返回 -1 表示没有连接到此 CPU
INIT_TEXT int nmi_lint(int cpu) {
    ASSERT(cpu >= 0);
    ASSERT(cpu < cpu_count());

    if (UINT32_MAX != g_nmi_gsi) {
        return -1;
    }

    if ((UINT32_MAX == g_nmi_cpu) || (g_loapics[cpu].processor_id == g_nmi_cpu)) {
        return g_nmi_lint;
    }

    return -1;
}

// 解析 MPS INTI flags，描述中断触发条件
void show_inti_flags(uint16_t flags) {
    const char *trigger = "confirm";
    const char *polarity = "confirm";

    switch (TRIGMODE_MASK & flags) {
    case TRIGMODE_EDGE: trigger = "edge"; break;
    case TRIGMODE_LEVEL: trigger = "level"; break;
    case TRIGMODE_CONFIRM:
    default:
        break;
    }

    switch (POLARITY_MASK & flags) {
    case POLARITY_LOW: polarity = "low"; break;
    case POLARITY_HIGH: polarity = "high"; break;
    case POLARITY_CONFIRM:
    default:
        break;
    }

    klog("%s-triggered, active-%s\n", trigger, polarity);
}

#if 0
static void print_madt(const madt_t *madt) {
    int len = madt->header.length;
    uint8_t *arr = (uint8_t *)madt;
    for (int i = 0; i < len; ++i) {
        klog("%02x", arr[i]);
    }
    klog("\n");

    klog("madt flags=%x\n", madt->flags);
    klog("madt loapic_addr=%x\n", madt->loapic_addr);

    for (size_t i = sizeof(madt_t); i < madt->header.length;) {
        acpi_subtbl_t *sub = (acpi_subtbl_t *)((size_t)madt + i);
        i += sub->length;
        switch (sub->type) {
        case MADT_TYPE_LOCAL_APIC_OVERRIDE:
            klog("Local APIC Address Override, addr=0x%lx\n",
                ((madt_loapic_override_t *)sub)->address);
            break;
        case MADT_TYPE_LOCAL_APIC: {
            madt_loapic_t *lo = (madt_loapic_t *)sub;
            klog("Processor Local APIC, processor-id=%u, apic-id=%u, flags=0x%x\n",
                lo->processor_id, lo->id, lo->loapic_flags);
            break;
        }
        case MADT_TYPE_IO_APIC: {
            madt_ioapic_t *io = (madt_ioapic_t *)sub;
            klog("IO APIC, io_apic_id=%d, addr=%x, global_system_interrupt_base=%u\n",
                io->id, io->address, io->gsi_base);
            break;
        }
        case MADT_TYPE_INTERRUPT_OVERRIDE: {
            madt_int_override_t *override = (madt_int_override_t *)sub;
            klog("Interrupt Source Override, bus=%d, source=%d, global_system_interrupt=%u, flags=0x%x\n",
                override->bus, override->source, override->gsi, override->inti_flags);
            break;
        }
        case MADT_TYPE_NMI_SOURCE: {
            madt_nmi_t *nmi = (madt_nmi_t *)sub;
            klog("NMI Source, global_system_interrupt=%u, flags=0x%x\n",
                nmi->gsi, nmi->inti_flags);
            break;
        }
        case MADT_TYPE_LOCAL_APIC_NMI: {
            madt_loapic_nmi_t *nmi = (madt_loapic_nmi_t *)sub;
            klog("Local APIC NMI, processor-id=%u, flags=0x%x, lint=%d\n",
                nmi->processor_id, nmi->inti_flags, nmi->lint);
            break;
        }
        default:
            klog("unknown subtable %u\n", sub->type);
            break;
        }
    }
}
#endif // 0

INIT_TEXT void parse_madt(const madt_t *madt) {
    ASSERT(NULL == g_loapics);
    ASSERT(NULL == g_ioapics);
    ASSERT(NULL == g_irq_to_gsi);
    ASSERT(NULL == g_gsi_to_irq);
    ASSERT(NULL == g_gsi_flags);
    ASSERT(NULL != madt);

#if 0
    print_madt(madt);
#endif

    g_loapic_addr = madt->loapic_addr;
    g_loapic_num = 0;
    g_ioapic_num = 0;
    g_irq_max = 0;
    g_gsi_max = 0;

    for (size_t i = sizeof(madt_t); i < madt->header.length;) {
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
        g_gsi_flags[i] = TRIGMODE_EDGE; // TODO 设置传统 IRQ 的默认触发条件
    }

    // 再次遍历 MADT，创建设备
    int loapic_idx = 0;
    int ioapic_idx = 0;
    // klog("MADT processor info:\n");
    for (size_t i = sizeof(madt_t); i < madt->header.length;) {
        acpi_subtbl_t *sub = (acpi_subtbl_t *)((size_t)madt + i);
        i += sub->length;

        switch (sub->type) {
        case MADT_TYPE_LOCAL_APIC: {
            madt_loapic_t *lo = (madt_loapic_t *)sub;
            if (1 & lo->loapic_flags) {
                // klog("  * CPU apicId=%u, processorId=%u\n", lo->id, lo->processor_id);
                g_loapics[loapic_idx].apic_id      = lo->id;
                g_loapics[loapic_idx].processor_id = lo->processor_id;
                g_loapics[loapic_idx].flags        = lo->loapic_flags;
                ++loapic_idx;
            }
            break;
        }
        case MADT_TYPE_LOCAL_X2APIC: {
            madt_lox2apic_t *lo = (madt_lox2apic_t *)sub;
            if (1 & lo->loapic_flags) {
                // klog("  * CPU (x2) apicId=%u, processorId=%u\n", lo->id, lo->processor_id);
                g_loapics[loapic_idx].apic_id      = lo->id;
                g_loapics[loapic_idx].processor_id = lo->processor_id;
                g_loapics[loapic_idx].flags        = lo->loapic_flags;
                ++loapic_idx;
            }
            break;
        }
        case MADT_TYPE_IO_APIC: {
            madt_ioapic_t *io = (madt_ioapic_t *)sub;
            // klog("  * IO APIC apicId=%u, gsi=%u, addr=0x%x\n", io->id, io->gsi_base, io->address);
            g_ioapics[ioapic_idx].apic_id  = io->id;
            g_ioapics[ioapic_idx].gsi_base = io->gsi_base;
            g_ioapics[ioapic_idx].address  = io->address;
            ++ioapic_idx;
            break;
        }
        case MADT_TYPE_INTERRUPT_OVERRIDE: {
            madt_int_override_t *override = (madt_int_override_t *)sub;
            g_irq_to_gsi[override->source] = override->gsi;
            g_gsi_to_irq[override->gsi] = override->source;
            g_gsi_flags[override->gsi] = override->inti_flags;
            // klog("  * override %d to %u, ", override->source, override->gsi);
            // show_inti_flags(override->inti_flags);
            break;
        }
        default:
            break;
        }
    }

    // 第三次遍历 MADT，记录 NMI 信息
    // klog("MADT nmi info:\n");
    for (size_t i = sizeof(madt_t); i < madt->header.length;) {
        acpi_subtbl_t *sub = (acpi_subtbl_t *)((size_t)madt + i);
        i += sub->length;

        switch (sub->type) {
        case MADT_TYPE_NMI_SOURCE: {
            // 描述了 NMI 连接到哪个 IO APIC，以及连接到哪个引脚
            madt_nmi_t *nmi = (madt_nmi_t *)sub;
            // klog("  - NMI_SOURCE gsi=%u\n", nmi->gsi);
            g_nmi_gsi = nmi->gsi;
            break;
        }
        case MADT_TYPE_LOCAL_APIC_NMI: {
            // 描述了 NMI 连接到哪个 Local APIC 的哪个 LINT 引脚，不经过 IO APIC
            madt_loapic_nmi_t *nmi = (madt_loapic_nmi_t *)sub;
            // klog("  - LOCAL_APIC_NMI proc-id=%d, lint=%d, ", nmi->processor_id, nmi->lint);
            // show_inti_flags(nmi->inti_flags);
            if (0xff == nmi->processor_id) {
                g_nmi_cpu = -1;
            } else {
                g_nmi_cpu = nmi->processor_id;
            }
            g_nmi_lint = nmi->lint;
            g_nmi_inti = nmi->inti_flags;
            break;
        }
        case MADT_TYPE_LOCAL_X2APIC_NMI: {
            madt_lox2apic_nmi_t *nmi = (madt_lox2apic_nmi_t *)sub;
            // klog("  - LOCAL_X2APIC_NMI proc-id=%d, lint=%d, ", nmi->processor_id, nmi->lint);
            // show_inti_flags(nmi->inti_flags);
            if (0xffffffff == nmi->processor_id) {
                g_nmi_cpu = -1;
            } else {
                g_nmi_cpu = nmi->processor_id;
            }
            g_nmi_lint = nmi->lint;
            g_nmi_inti = nmi->inti_flags;
            break;
        }
        default:
            break;
        }
    }
}
