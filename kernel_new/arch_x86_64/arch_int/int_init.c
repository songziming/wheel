#include "int_init.h"
#include "ioapic.h"
#include "loapic.h"
#include <arch_intf.h>

#include <generic/gdt_idt_tss.h>
#include <memory/percpu.h>
#include <library/debug.h>


void *g_handlers[256];

PCPU_DATA int   g_int_depth = 0;
PCPU_DATA void *g_int_stack = NULL;




// static enum {
//     IO_APIC,

// }
// // NMI 连接到哪个 IO APIC
// static CONST uint32_t g_nmi_gsi = 0xffffffffU; // -1 表示不经过 IO APIC，直接连接 Local APIC

// // NMI 直接连接到哪个处理器的哪个 LINT 引脚
// static CONST uint32_t g_nmi_cpu = 0xffffffffU; // -1 表示连接到所有处理器
// static CONST uint8_t  g_nmi_lint = 0;
// static CONST uint8_t  g_nmi_inti = 0;   // 触发模式（edge/level、low/high）




//------------------------------------------------------------------------------
// 解析 madt，记录 APIC 信息
//------------------------------------------------------------------------------

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
    // 把 NMI 信息记录在 ioapic、loapic 结构体中
    for (size_t i = sizeof(madt_t); i < madt->header.length;) {
        acpi_subtbl_t *sub = (acpi_subtbl_t *)((size_t)madt + i);
        i += sub->length;

        switch (sub->type) {
        case MADT_TYPE_NMI_SOURCE: {
            // 描述了 NMI 连接到哪个 IO APIC，以及连接到哪个引脚
            madt_nmi_t *nmi = (madt_nmi_t *)sub;
            log("NMI connects to GSI %d\n", nmi->gsi);
            break;
        }
        case MADT_TYPE_LOCAL_APIC_NMI: {
            // 描述了 NMI 连接到哪个 Local APIC 的哪个 LINT 引脚，不经过 IO APIC
            madt_loapic_nmi_t *nmi = (madt_loapic_nmi_t *)sub;
            if (0xff == nmi->processor_id) {
                log("NMI connects to all cpu, lint %d, flags %x\n",
                    nmi->lint, nmi->inti_flags);
            } else {
                log("NMI connects to cpu %d, lint %d, flags %x\n",
                    nmi->processor_id, nmi->lint, nmi->inti_flags);
            }
            break;
        }
        case MADT_TYPE_LOCAL_X2APIC_NMI: {
            madt_lox2apic_nmi_t *nmi = (madt_lox2apic_nmi_t *)sub;
            if (0xffffffff == nmi->processor_id) {
                log("NMI connects to all cpu, lint %d, flags %x\n",
                    nmi->lint, nmi->inti_flags);
            } else {
                log("NMI connects to cpu %d, lint %d, flags %x\n",
                    nmi->processor_id, nmi->lint, nmi->inti_flags);
            }
            break;
        }
        default:
            break;
        }
    }
}



//------------------------------------------------------------------------------
// default handler
//------------------------------------------------------------------------------

static void handle_exception(int vec) {
    log("[CPU%d] exception 0x%x\n", cpu_index(), vec);
    cpu_halt();
}

static void handle_interrupt(int vec) {
    log("[CPU%d] interrupt 0x%x\n", cpu_index(), vec);
    // cpu_halt();
}




INIT_TEXT void int_init() {
    // 填充所有 TSS 的 IST 字段
    for (int i = 0; i < cpu_count(); ++i) {
        tss_set_ist(i, 1, percpu_nmi_stack_top(i));
        tss_set_ist(i, 2, percpu_df_stack_top(i));
        tss_set_ist(i, 3, percpu_pf_stack_top(i));
        tss_set_ist(i, 4, percpu_mc_stack_top(i));

        int *pdepth = percpu_ptr(i, &g_int_depth);
        void **pstack = percpu_ptr(i, &g_int_stack);
        *pdepth = 0;
        *pstack = (void *)percpu_int_stack_top(i);
    }

    // 在 IDT 里面填入 IST 编号
    idt_set_ist(2,  1); // NMI
    idt_set_ist(8,  2); // #DF
    idt_set_ist(14, 3); // #PF
    idt_set_ist(18, 4); // #MC

    // 设置中断处理函数
    for (int i = 0; i < 32; ++i) {
        g_handlers[i] = handle_exception;
    }
    for (int i = 32; i < 256; ++i) {
        g_handlers[i] = handle_interrupt;
    }
}
