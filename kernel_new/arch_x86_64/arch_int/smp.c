#include "smp.h"
#include "ioapic.h"
#include "loapic.h"
#include <arch_intf.h>
#include <devices/acpi_madt.h>
#include <memory/early_alloc.h>
#include <library/debug.h>



// 解析多核信息
// TODO 可以改名为 apic_init、madt_init，或者合并到 int_init


// NMI 连接到哪个 IO APIC
static CONST uint32_t g_nmi_gsi = 0xffffffffU; // -1 表示不经过 IO APIC，直接连接 Local APIC

// NMI 直接连接到哪个处理器的哪个 LINT 引脚
static CONST uint32_t g_nmi_cpu = 0xffffffffU; // -1 表示连接到所有处理器
static CONST uint8_t  g_nmi_lint = 0;
static CONST uint8_t  g_nmi_inti = 0;   // 触发模式（edge/level、low/high）



// 遍历 MADT 两次，首先统计 loapic、ioapic 个数，然后申请空间，复制信息
INIT_TEXT void parse_madt(madt_t *madt) {
    size_t loapic_addr = madt->loapic_addr;
    int loapic_num = 0;
    int ioapic_num = 0;
    uint32_t irq_max = 0;
    uint32_t gsi_max = 0;

    // 第一次遍历，统计数量
    for (size_t i = sizeof(madt_t); i < madt->header.length;) {
        acpi_subtbl_t *sub = (acpi_subtbl_t *)((size_t)madt + i);
        i += sub->length;

        switch (sub->type) {
        case MADT_TYPE_LOCAL_APIC_OVERRIDE:
            loapic_addr = ((madt_loapic_override_t *)sub)->address;
            break;
        case MADT_TYPE_LOCAL_APIC: {
            madt_loapic_t *lo = (madt_loapic_t *)sub;
            if ((1 & lo->loapic_flags) && (loapic_num < MAX_CPU_COUNT)) {
                ++loapic_num;
            }
            break;
        }
        case MADT_TYPE_LOCAL_X2APIC: {
            madt_lox2apic_t *lo = (madt_lox2apic_t *)sub;
            if ((1 & lo->loapic_flags) && (loapic_num < MAX_CPU_COUNT)) {
                ++loapic_num;
            }
            break;
        }
        case MADT_TYPE_IO_APIC:
            ++ioapic_num;
            break;
        case MADT_TYPE_INTERRUPT_OVERRIDE: {
            madt_int_override_t *override = (madt_int_override_t *)sub;
            if (override->source > irq_max) {
                irq_max = override->source;
            }
            if (override->gsi > gsi_max) {
                gsi_max = override->gsi;
            }
            break;
        }
        case MADT_TYPE_NMI_SOURCE: {
            madt_nmi_t *nmi = (madt_nmi_t *)sub;
            if (nmi->gsi > gsi_max) {
                gsi_max = nmi->gsi;
            }
            break;
        }
        default:
            break;
        }
    }

    loapic_alloc(loapic_addr, loapic_num);
    ioapic_alloc(ioapic_num, irq_max, gsi_max);

    // 第二次遍历，记录信息
    int loapic_idx = 0;
    int ioapic_idx = 0;
    for (size_t i = sizeof(madt_t); i < madt->header.length;) {
        acpi_subtbl_t *sub = (acpi_subtbl_t *)((size_t)madt + i);
        i += sub->length;
        switch (sub->type)  {
        case MADT_TYPE_LOCAL_APIC:
            loapic_parse(loapic_idx++, (madt_loapic_t *)sub);
            break;
        case MADT_TYPE_LOCAL_X2APIC:
            loapic_parse_x2(loapic_idx++, (madt_lox2apic_t *)sub);
            break;
        case MADT_TYPE_IO_APIC:
            ioapic_parse(ioapic_idx++, (madt_ioapic_t *)sub);
            break;
        case MADT_TYPE_INTERRUPT_OVERRIDE:
            override_int((madt_int_override_t *)sub);
            break;
        default:
            break;
        }
    }
    ASSERT(loapic_idx == loapic_num);
    ASSERT(ioapic_idx == ioapic_num);

    // 第三次遍历 MADT，记录 NMI 信息
    //      NMI 信息需要在某个 IO APIC 的中断信息表中体现
    //      因此要等 IO APIC 记录完成再统计
    for (size_t i = sizeof(madt_t); i < madt->header.length;) {
        acpi_subtbl_t *sub = (acpi_subtbl_t *)((size_t)madt + i);
        i += sub->length;

        switch (sub->type) {
        case MADT_TYPE_NMI_SOURCE: {
            // 描述了 NMI 连接到哪个 IO APIC，以及连接到哪个引脚
            madt_nmi_t *nmi = (madt_nmi_t *)sub;
            g_nmi_gsi = nmi->gsi;
            log("NMI connects to GSI %d\n", g_nmi_gsi);
            break;
        }
        case MADT_TYPE_LOCAL_APIC_NMI: {
            // 描述了 NMI 连接到哪个 Local APIC 的哪个 LINT 引脚，不经过 IO APIC
            madt_loapic_nmi_t *nmi = (madt_loapic_nmi_t *)sub;
            if (0xff == nmi->processor_id) {
                g_nmi_cpu = -1;
            } else {
                g_nmi_cpu = nmi->processor_id;
            }
            g_nmi_lint = nmi->lint;
            g_nmi_inti = nmi->inti_flags;
            log("NMI connects to cpu %d, lint %d\n", g_nmi_cpu, g_nmi_lint);
            break;
        }
        case MADT_TYPE_LOCAL_X2APIC_NMI: {
            madt_lox2apic_nmi_t *nmi = (madt_lox2apic_nmi_t *)sub;
            if (0xffffffff == nmi->processor_id) {
                g_nmi_cpu = -1; // 连接到所有处理器的 LINT
            } else {
                g_nmi_cpu = nmi->processor_id;
            }
            g_nmi_lint = nmi->lint;
            g_nmi_inti = nmi->inti_flags;
            log("NMI connects to cpu %d, lint %d\n", g_nmi_cpu, g_nmi_lint);
            break;
        }
        default:
            break;
        }
    }

}

