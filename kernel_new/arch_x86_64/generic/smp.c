#include "smp.h"
#include <devices/acpi.h>
#include <devices/acpi_madt.h>
#include <early_alloc.h>
#include <debug.h>


//------------------------------------------------------------------------------
// 解析多核信息
//------------------------------------------------------------------------------

uint64_t g_loapic_addr;

// 这几个变量适合改为全局变量
static CONST int g_loapic_num = 0;
static CONST int g_ioapic_num = 0;
static CONST uint32_t g_gsi_max = 0;
static CONST uint32_t g_irq_max = 0;

static CONST loapic_t *g_loapics = NULL;
static CONST ioapic_t *g_ioapics = NULL;
static CONST uint32_t *g_irq_to_gsi = NULL;
static CONST uint8_t  *g_gsi_to_irq = NULL;
static CONST uint16_t *g_gsi_flags  = NULL;

// 遍历 MADT 两次，首先统计 loapic、ioapic 个数，然后申请空间，复制信息
INIT_TEXT void parse_madt(madt_t *madt) {
    g_loapic_addr = madt->loapic_addr;

    // 第一次遍历，统计数量
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
                // if (g_max_id < lo->id) {
                //     g_max_id = lo->id;
                // }
            }
            break;
        }
        case MADT_TYPE_LOCAL_X2APIC: {
            madt_lox2apic_t *lo = (madt_lox2apic_t *)sub;
            if ((1 & lo->loapic_flags) && (g_loapic_num < MAX_CPU_COUNT)) {
                ++g_loapic_num;
                // if (g_max_id < lo->id) {
                //     g_max_id = lo->id;
                // }
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

    g_loapics = early_alloc_ro(g_loapic_num * sizeof(loapic_t));
    g_ioapics = early_alloc_ro(g_ioapic_num * sizeof(ioapic_t));
    g_irq_to_gsi = early_alloc_ro((g_irq_max + 1) * sizeof(uint32_t));
    g_gsi_to_irq = early_alloc_ro((g_gsi_max + 1) * sizeof(uint8_t));
    g_gsi_flags  = early_alloc_ro((g_gsi_max + 1) * sizeof(uint16_t));

    // 默认情况下，8259 IRQ 0~15 与 GSI 0~15 对应
    // 传统 ISA 中断为边沿触发
    for (uint8_t i = 0; i < g_irq_max; ++i) {
        g_irq_to_gsi[i] = i;
    }
    for (uint32_t i = 0; (i < g_gsi_max) && (i < 256); ++i) {
        g_gsi_to_irq[i] = i;
        g_gsi_flags[i] = TRIGMODE_EDGE;
    }

    // 第二次遍历，记录信息
    int loapic_idx = 0;
    int ioapic_idx = 0;
    for (size_t i = sizeof(madt_t); i < madt->header.length;) {
        acpi_subtbl_t *sub = (acpi_subtbl_t *)((size_t)madt + i);
        i += sub->length;

        switch (sub->type)  {
        case MADT_TYPE_LOCAL_APIC: {
            madt_loapic_t *lo = (madt_loapic_t *)sub;
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
            break;
        }
        default:
            break;
        }
    }
    ASSERT(loapic_idx == g_loapic_num);
    ASSERT(ioapic_idx == g_ioapic_num);

    // TODO 第三次遍历 MADT，记录 NMI 信息
    //      NMI 信息需要在某个 IO APIC 的中断信息表中体现
    //      因此要等 IO APIC 记录完成再统计

}


//------------------------------------------------------------------------------
// 实现 interface
//------------------------------------------------------------------------------

// inline int cpu_count() {
//     ASSERT(0 != g_loapic_num);
//     return g_loapic_num;
// }

// inline int cpu_index() {
//     return 0;
// }
