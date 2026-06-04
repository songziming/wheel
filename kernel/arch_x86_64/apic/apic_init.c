#include "apic.h"
#include <cpu/features.h>
#include <early_alloc.h>
#include <debug.h>



CONST int       g_loapic_num;
CONST size_t    g_loapic_addr;
CONST loapic_t *g_loapics;

CONST int       g_ioapic_num;
CONST ioapic_t *g_ioapics;

// GSI<->IRQ 双向映射表
static CONST uint8_t   g_irq_max = 0;
static CONST uint32_t  g_gsi_max = 0;
static CONST uint32_t *g_irq_to_gsi = NULL;
static CONST uint8_t  *g_gsi_to_irq = NULL;
static CONST uint8_t  *g_gsi_modes = NULL; // 记录该中断的 polarity、trigger level
#define GSI_MODE_EDGE 1 // edge-triggered
#define GSI_MODE_HIGH 2 // active-high


INIT_TEXT int irq_to_gsi(uint8_t irq) {
    if (irq <= g_irq_max) {
        return g_irq_to_gsi[irq];
    } else {
        return (int)irq;
    }
}

INIT_TEXT int gsi_is_edge(uint32_t gsi) {
    if (gsi <= g_gsi_max) {
        return g_gsi_modes[gsi] & GSI_MODE_EDGE;
    } else {
        return 1; // 默认是 edge-trigger
    }
}

INIT_TEXT int gsi_is_high(uint32_t gsi) {
    if (gsi <= g_gsi_max) {
        return g_gsi_modes[gsi] & GSI_MODE_HIGH;
    } else {
        return 1; // 默认是 active-high
    }
}



static INIT_TEXT void add_io_apic(ioapic_t *dst, const madt_ioapic_t *tbl) {
    dst->apic_id  = tbl->id;
    dst->gsi_base = tbl->gsi_base;
    dst->addr     = tbl->address;
}

static INIT_TEXT void add_local_apic(loapic_t *dst, const madt_loapic_t *tbl) {
    dst->apic_id      = tbl->id;
    dst->processor_id = tbl->processor_id;
    dst->flags        = tbl->loapic_flags;
}

static INIT_TEXT void add_local_x2apic(loapic_t *dst, const madt_lox2apic_t *tbl) {
    dst->apic_id      = tbl->id;
    dst->processor_id = tbl->processor_id;
    dst->flags        = tbl->loapic_flags;
}

static INIT_TEXT void override_int(madt_int_override_t *tbl) {
    g_irq_to_gsi[tbl->source] = tbl->gsi;
    g_gsi_to_irq[tbl->gsi] = tbl->source;

    switch (TRIGMODE_MASK & tbl->inti_flags) {
    case TRIGMODE_LEVEL: g_gsi_modes[tbl->gsi] &= ~GSI_MODE_EDGE; break;
    case TRIGMODE_EDGE:  g_gsi_modes[tbl->gsi] |=  GSI_MODE_EDGE; break;
    default: break;
    }

    switch (POLARITY_MASK & tbl->inti_flags) {
    case POLARITY_LOW:  g_gsi_modes[tbl->gsi] &= ~GSI_MODE_HIGH; break;
    case POLARITY_HIGH: g_gsi_modes[tbl->gsi] |=  GSI_MODE_HIGH; break;
    default: break;
    }
}

static INIT_TEXT void connect_nmi(uint32_t processor_id, uint8_t lint, uint16_t flags) {
    if (0xffffffffU == processor_id) {
        for (int i = 0; i < g_loapic_num; ++i) {
            g_loapics[i].nmi_lint = lint;
            g_loapics[i].nmi_flags = flags;
        }
        return;
    }

    for (int i = 0; i < g_loapic_num; ++i) {
        if (g_loapics[i].processor_id == processor_id) {
            g_loapics[i].nmi_lint = lint;
            g_loapics[i].nmi_flags = flags;
            break;
        }
    }
}

// 解析 MADT，获取 apic 信息
INIT_TEXT void parse_madt(madt_t *madt) {
    g_loapic_addr = (size_t)madt->loapic_addr;

    // 统计 local apic、io apic 个数，irq-gsi 映射表长度
    g_loapic_num = 0;
    g_ioapic_num = 0;
    g_irq_max = 0;
    g_gsi_max = 0;
    for (size_t i = sizeof(madt_t); i < madt->header.length;) {
        acpi_subtbl_t *sub = (acpi_subtbl_t*)((size_t)madt + i);
        i += sub->length;

        switch (sub->type) {
        case MADT_TYPE_LOCAL_APIC_OVERRIDE:
            g_loapic_addr = ((madt_loapic_override_t*)sub)->address;
            break;
        case MADT_TYPE_LOCAL_APIC:
            g_loapic_num += ((madt_loapic_t*)sub)->loapic_flags & 1;
            break;
        case MADT_TYPE_LOCAL_X2APIC:
            g_loapic_num += ((madt_lox2apic_t*)sub)->loapic_flags & 1;
            break;
        case MADT_TYPE_IO_APIC:
            ++g_ioapic_num;
            break;
        case MADT_TYPE_INTERRUPT_OVERRIDE: {
            madt_int_override_t *override = (madt_int_override_t*)sub;
            if (override->source > g_irq_max) {
                g_irq_max = override->source;
            }
            if (override->gsi > g_gsi_max) {
                g_gsi_max = override->gsi;
            }
            break;
        }
        case MADT_TYPE_NMI_SOURCE: {
            madt_nmi_t *nmi = (madt_nmi_t*)sub;
            if (nmi->gsi > g_gsi_max) {
                g_gsi_max = nmi->gsi;
            }
            break;
        }
        default:
            break;
        }
    }

    g_loapics = early_alloc_ro(g_loapic_num * sizeof(loapic_t));
    g_ioapics = early_alloc_ro(g_ioapic_num * sizeof(ioapic_t));
    g_irq_to_gsi = early_alloc_ro((g_irq_max + 1) * sizeof(uint32_t));
    g_gsi_to_irq = early_alloc_ro((g_gsi_max + 1) * sizeof(uint8_t));
    g_gsi_modes  = early_alloc_ro((g_gsi_max + 1) * sizeof(uint8_t));

    // 初始化
    for (int i = 0; i < g_loapic_num; ++i) {
        g_loapics[i].nmi_lint = -1; // 默认不连接到 NMI
    }

    // 默认情况下，8259 IRQ 0~15 与 GSI 0~15 一一对应
    for (uint8_t i = 0; i < g_irq_max; ++i) {
        g_irq_to_gsi[i] = i;
    }

    // ISA 规定中断是上升沿触发的，来自 osdev brendan
    // https://forum.osdev.org/viewtopic.php?p=280231&sid=e9735f17f9d4b5ab07e6bedfa1ea9b8b#p280231
    // TODO ACPI 规范里的 polarity 是否也能描述升降沿？还是只描述高低电平？
    for (uint32_t i = 0; i < g_gsi_max; ++i) {
        g_gsi_to_irq[i] = i;
        g_gsi_modes[i] = GSI_MODE_EDGE | GSI_MODE_HIGH;
    }

    // 第二次遍历，记录 local apic、io apic、gsi 映射表的详细信息
    int lo_idx = 0;
    int io_idx = 0;
    for (size_t i = sizeof(madt_t); i < madt->header.length;) {
        acpi_subtbl_t *sub = (acpi_subtbl_t*)((size_t)madt + i);
        i += sub->length;
        switch (sub->type)  {
        case MADT_TYPE_LOCAL_APIC:
            if (((madt_loapic_t*)sub)->loapic_flags & 1) {
                add_local_apic(&g_loapics[lo_idx++], (madt_loapic_t*)sub);
            }
            break;
        case MADT_TYPE_LOCAL_X2APIC:
            if (((madt_lox2apic_t*)sub)->loapic_flags & 1) {
                add_local_x2apic(&g_loapics[lo_idx++], (madt_lox2apic_t*)sub);
            }
            break;
        case MADT_TYPE_IO_APIC:
            add_io_apic(&g_ioapics[io_idx++], (madt_ioapic_t*)sub);
            break;
        case MADT_TYPE_INTERRUPT_OVERRIDE:
            override_int((madt_int_override_t*)sub);
            break;
        default:
            break;
        }
    }
    ASSERT(lo_idx == g_loapic_num);
    ASSERT(io_idx == g_ioapic_num);

    // 第三次遍历 madt，记录 NMI 信息
    // NMI 是一种特殊类型的外部中断，某些服务器上可以由物理按钮触发
    // 还有一些硬件将 NMI 用作看门狗，系统无响应则发送 NMI 触发系统复位
    for (size_t i = sizeof(madt_t); i < madt->header.length;) {
        acpi_subtbl_t *sub = (acpi_subtbl_t*)((size_t)madt + i);
        i += sub->length;

        switch (sub->type) {
        case MADT_TYPE_NMI_SOURCE: {
            // 找出 nmi 连接到哪个 IO Apic，连接到哪个引脚
            // TODO 在 IO APIC 里面标记 NMI
            madt_nmi_t *nmi = (madt_nmi_t*)sub;
            logk("NMI connects to GSI %d\n", nmi->gsi);
            break;
        }
        case MADT_TYPE_LOCAL_APIC_NMI: {
            // 找出 nmi 连接到哪个 Local APIC 的哪个 LINT 引脚
            madt_loapic_nmi_t *nmi = (madt_loapic_nmi_t*)sub;
            uint32_t dst = (uint32_t)nmi->processor_id;
            if (0xff == dst) { dst = 0xffffffffU; }
            connect_nmi(dst, nmi->lint, nmi->inti_flags);
            break;
        }
        case MADT_TYPE_LOCAL_X2APIC_NMI: {
            // 同上
            madt_lox2apic_nmi_t *nmi = (madt_lox2apic_nmi_t*)sub;
            connect_nmi(nmi->processor_id, nmi->lint, nmi->inti_flags);
            break;
        }
        default:
            break;
        }
    }
}
