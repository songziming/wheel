#include <arch_smp.h>
#include <cpu/info.h>
#include <wheel.h>
#include <shell.h>


// 本文件主要负责解析 MADT，提取 Local APIC 和 IO APIC 的数据


CONST size_t g_loapic_addr;
CONST int    g_loapic_num = 0;
CONST int    g_ioapic_num = 0;

CONST loapic_t *g_loapics = NULL;
CONST ioapic_t *g_ioapics = NULL;

// IRQ 与 GSI 的映射表
// 这里的 irq_max、gsi_max 指的是映射表条目数量，并非中断向量的个数
static CONST uint8_t   g_irq_max;
static CONST uint32_t  g_gsi_max;
static CONST uint32_t *g_irq_to_gsi = NULL;
static CONST uint8_t  *g_gsi_to_irq = NULL;
static CONST uint16_t *g_gsi_flags  = NULL;

// NMI 连接到哪个 IO APIC
static CONST uint32_t g_nmi_gsi = 0xffffffffU; // -1 表示不经过 IO APIC，直接连接 Local APIC

// NMI 直接连接到哪个处理器的哪个 LINT 引脚
static CONST uint32_t g_nmi_cpu = 0xffffffffU; // -1 表示连接到所有处理器
static CONST uint8_t  g_nmi_lint = 0;
static CONST uint8_t  g_nmi_inti = 0;   // 触发模式（edge/level、low/high）

// 最大的 x2APIC ID
// 如果能用 8-bit 表示，说明仍兼容 IO APIC
// 如果超过了 8-bit，则只能借助 VT-d Interrupt Remapper
// 必须首先启用 extended interrupt mode，然后再开启 x2APIC 模式
static CONST uint32_t g_max_id = 0;

static CONST const madt_t *g_madt = NULL;
static shell_cmd_t g_cmd_smp;



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


static void print_madt(const madt_t *madt) {
    if (NULL == madt) {
        klog("madt is NULL\n");
        return;
    }

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

static int show_smp(UNUSED int argc, UNUSED char *argv[]) {
    print_madt(g_madt);
    return 0;
}

INIT_TEXT void parse_madt(const madt_t *madt) {
    ASSERT(NULL == g_loapics);
    ASSERT(NULL == g_ioapics);
    ASSERT(NULL == g_irq_to_gsi);
    ASSERT(NULL == g_gsi_to_irq);
    ASSERT(NULL == g_gsi_flags);
    ASSERT(NULL != madt);

    g_madt = madt;

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
            if ((1 & lo->loapic_flags) && (g_loapic_num < MAX_CPU_COUNT)) {
                ++g_loapic_num;
                if (g_max_id < lo->id) {
                    g_max_id = lo->id;
                }
            }
            break;
        }
        case MADT_TYPE_LOCAL_X2APIC: {
            madt_lox2apic_t *lo = (madt_lox2apic_t *)sub;
            if ((1 & lo->loapic_flags) && (g_loapic_num < MAX_CPU_COUNT)) {
                ++g_loapic_num;
                if (g_max_id < lo->id) {
                    g_max_id = lo->id;
                }
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
    for (uint32_t i = 0; (i < g_gsi_max) && (i < 256); ++i) {
        g_gsi_to_irq[i] = i;
        g_gsi_flags[i] = TRIGMODE_EDGE; // 传统 IRQ 的默认触发条件
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

    // TODO 统计最大的 apic_id 取值，检查是否超过 256
    //      如果超过 256 且硬件没有 interrupt remapper，则不能使用 x2APIC
    //      需要在初始化 Local APIC 之前决定

    g_cmd_smp.name = "smp";
    g_cmd_smp.func = show_smp;
    shell_add_cmd(&g_cmd_smp);
}

inline int cpu_count() {
    ASSERT(0 != g_loapic_num);
    return g_loapic_num;
}

// 检查所有的 Local APIC ID，判断 IO APIC 使用的 8-bit logical destination
// 能否表示所有的目标 CPU
INIT_TEXT int requires_int_remap() {
    if (0 == (CPU_FEATURE_X2APIC & g_cpu_features)) {
        return 0; // xAPIC 没有任何问题
    }

    if (g_max_id >= 255) {
        return 1;
    }

    for (int i = 0; i < g_loapic_num; ++i) {
        uint16_t cluster = g_loapics[i].apic_id >> 4;
        uint16_t logical = 1 << (g_loapics[i].apic_id & 15);
        if ((cluster >= 15) || (logical >= 16)) {
            return 1;
        }
    }

    return 0;
}

// 判断 NMI 连接到指定 CPU 的哪个 LINT 引脚，返回 -1 表示没有连接到此 CPU
INIT_TEXT int get_nmi_lint(int cpu) {
    ASSERT(cpu >= 0);
    ASSERT(cpu < cpu_count());

    if (0xffffffffU != g_nmi_gsi) {
        return -1;
    }

    if ((0xffffffffU == g_nmi_cpu) || (g_loapics[cpu].processor_id == g_nmi_cpu)) {
        return g_nmi_lint;
    }

    return -1;
}

INIT_TEXT int get_gsi_for_irq(int irq) {
    if (irq <= g_irq_max) {
        return g_irq_to_gsi[irq];
    }
    return irq;
}


// ISA 默认是 edge-triggered
// EISA 默认是 level-triggered active-low
// 8259A PIC 中断触发条件是 edge-triggered high pin polarity（信息来自 IBM PC/AT Technical Reference）

INIT_TEXT int get_gsi_trigmode(int gsi) {
    if ((uint32_t)gsi <= g_gsi_max) {
        switch (TRIGMODE_MASK & g_gsi_flags[gsi]) {
        case TRIGMODE_EDGE:  return 1;  // edge
        case TRIGMODE_LEVEL: return 0;  // level
        default:             return 1;  // edge
        }
    }

    // TODO 返回默认的触发模式
    // IRQ 以后的中断怎么触发？
    return 0;
}

INIT_TEXT int get_gsi_polarity(int gsi) {
    if ((uint32_t)gsi <= g_gsi_max) {
        switch (POLARITY_MASK & g_gsi_flags[gsi]) {
        case POLARITY_HIGH: return 1;   // high
        case POLARITY_LOW:  return 0;   // low
        default:            return 1;   // high
        }
    }

    return 0;
}
