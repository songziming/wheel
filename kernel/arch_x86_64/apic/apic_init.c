#include "apic.h"
#include <cpu/features.h>
#include <early_alloc.h>
#include <debug.h>





// 判断是否需要配置 interrupt remapper
// 如果 local apic-id 超过了 8-bit，则无法在 IO APIC redirect entry 里表示
// 要么只让 apic-id 小于 8-bit 的 CPU 处理外部中断，要么必须使用 interrupt remapper
INIT_TEXT int need_int_remap() {
    uint32_t max_apicid = 0;
    char not_physical = 0;  // 无法使用 physical 模式定位每个 CPU
    char not_logical = 0;   // 无法使用 logical 模式定位每个 CPU
    for (int i = 0; i < g_loapic_num; ++i) {
        uint32_t apicid = g_loapics[i].apic_id;
        if (max_apicid < apicid) {
            max_apicid = apicid;
        }
        if (apicid >= 16) {
            not_physical = 1;
        }
        uint16_t cluster = apicid >> 4;
        uint16_t logical = 1 << (apicid & 15);
        if ((cluster >= 15) || (logical >= 16)) {
            not_logical = 1;
        }
    }

    // apic-id 超过了 255，无法使用 8-bit 表示
    // 某些 cpu 无法作为 IPI 目标，必然需要 remap
    if (max_apicid >= 255) {
        return 1;
    }

    // TODO 根据 CPU 数量，决定 IO APIC 使用 physical 还是 logical
    // TODO 可以增加一个函数，输入 CPU 编号，返回 8-bit dest 内容

    // 两种模式都无法表示，才说明必须 remap interrupts
    return not_physical & not_logical;
}



// 作用属于 io apic
INIT_TEXT void override_int(madt_int_override_t *tbl) {
    // ASSERT(g_irq_num > 0);
    // ASSERT(g_gsi_num > 0);

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


// 解析 MADT，获取 apic 信息
INIT_TEXT void parse_madt(madt_t *madt) {
    g_loapic_addr = (size_t)madt->loapic_addr;
    logk("local apic base = %zx\n", g_loapic_addr);

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
                loapic_parse(&g_loapics[lo_idx++], (madt_loapic_t*)sub);
            }
            break;
        case MADT_TYPE_LOCAL_X2APIC:
            if (((madt_lox2apic_t*)sub)->loapic_flags & 1) {
                loapic_parse_x2(&g_loapics[lo_idx++], (madt_lox2apic_t*)sub);
            }
            break;
        case MADT_TYPE_IO_APIC:
            ioapic_parse(&g_ioapics[io_idx++], (madt_ioapic_t*)sub);
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
            madt_nmi_t *nmi = (madt_nmi_t*)sub;
            logk("NMI connects to GSI %d\n", nmi->gsi);
            break;
        }
        case MADT_TYPE_LOCAL_APIC_NMI: {
            // 找出 nmi 连接到哪个 Local APIC 的哪个 LINT 引脚
            madt_loapic_nmi_t *nmi = (madt_loapic_nmi_t*)sub;
            if (0xff == nmi->processor_id) {
                logk("NMI connects to all cpu, LINT-%d, flags %x\n",
                    nmi->lint, nmi->inti_flags);
            } else {
                logk("NMI connects to cpu-%d, LINT-%d, flags %x\n",
                    nmi->processor_id, nmi->lint, nmi->inti_flags);
            }
            break;
        }
        case MADT_TYPE_LOCAL_X2APIC_NMI: {
            // 同上
            madt_lox2apic_nmi_t *nmi = (madt_lox2apic_nmi_t*)sub;
            if (0xffffffff == nmi->processor_id) {
                logk("x2 NMI connects to all cpu, LINT-%d, flags %x\n",
                    nmi->lint, nmi->inti_flags);
            } else {
                logk("x2 NMI connects to cpu-%d, LINT-%d, flags %x\n",
                    nmi->processor_id, nmi->lint, nmi->inti_flags);
            }
            break;
        }
        default:
            break;
        }
    }
}
