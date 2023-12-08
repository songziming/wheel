#include <arch_smp.h>
#include <arch_mem.h>
#include <liba/rw.h>
#include <liba/cpuid.h>
#include <dev/acpi_madt.h>

#include <arch_interface.h>
#include <debug.h>
#include <page.h>
#include <libk.h>



// 解析 MPS INTI flags
void show_mps_inti_flags(uint16_t flags) {
    // 获取中断触发模式
    const char *trig = "invalid";
    switch (flags & TRIGMODE_MASK) {
    case TRIGMODE_CONFIRM: trig = "confirm"; break;
    case TRIGMODE_EDGE:    trig = "edge";    break;
    case TRIGMODE_LEVEL:   trig = "level";   break;
    default: break;
    }

    // 获取极性（高电平还是低电平表示 1）
    const char *pola = "invalid";
    switch (flags & POLARITY_MASK) {
    case POLARITY_CONFIRM: pola = "confirm";     break;
    case POLARITY_HIGH:    pola = "active-high"; break;
    case POLARITY_LOW:     pola = "active-low";  break;
    default: break;
    }

    dbg_print("trigger-mode=%s, polarity=%s\n", trig, pola);
}


//------------------------------------------------------------------------------
// 检测 Local-APIC 和 IO-APIC
//------------------------------------------------------------------------------

// Local APIC 的数量也代表 CPU 个数
// 但 OS 能支持的 CPU 数量是有限的
CONST size_t    g_loapic_addr;    // 内存映射地址（物理地址）
CONST int       g_loapic_count;       // x2APIC 架构下最多 4G-1 个节点
CONST int       g_ioapic_count;
CONST loapic_t *g_loapics;
CONST ioapic_t *g_ioapics;

// 记录 IRQ 和 GSI 的对应关系
// 这里的 gsi_num、irq_num 并非表示 GSI、IRQ 的数量，而是数组长度
static CONST int       g_irq_num;
static CONST int       g_gsi_num;
static CONST int      *g_irq_to_gsi = NULL;
static CONST int      *g_gsi_to_irq = NULL;
static CONST uint16_t *g_gsi_flags  = NULL;


// 解析 MADT，找出所有的 IO APIC、Local APIC
// MADT 不需要保存，ACPI 表会一直保留
INIT_TEXT void parse_madt(madt_t *tbl) {
    g_loapic_addr = (size_t)tbl->loapic_addr;

    // 第一次遍历 MADT，统计 Local APIC、IO APIC 的个数
    // 以及重定位表中 GSI、IRQ 的最大值，从而知道需要申请多少内存
    // 还有 Local APIC 内存映射地址
    int loapic_idx = 0;
    int ioapic_idx = 0;
    int irq_max = 0;
    int gsi_max = 0;
    uint8_t *ptr = (uint8_t *)tbl + sizeof(madt_t);
    uint8_t *end = (uint8_t *)tbl + tbl->header.length;
    while (ptr < end) {
        acpi_subtbl_t *sub = (acpi_subtbl_t *)ptr;
        ptr += sub->length;

        switch (sub->type) {
        case MADT_TYPE_LOCAL_APIC_OVERRIDE:
            g_loapic_addr = ((madt_loapic_override_t *)sub)->address;
            break;
        case MADT_TYPE_LOCAL_APIC: {
            madt_loapic_t *lo = (madt_loapic_t *)sub;
            if ((lo->loapic_flags & 1) && (loapic_idx < INT_MAX)) {
                ++loapic_idx;
            }
            break;
        }
        case MADT_TYPE_LOCAL_X2APIC: {
            // x2APIC 架构下最多 4G-1 个节点，但我们不支持那么多
            madt_lox2apic_t *x2 = (madt_lox2apic_t *)sub;
            if ((x2->loapic_flags & 1) && (loapic_idx < INT_MAX)) {
                ++loapic_idx;
            }
            break;
        }
        case MADT_TYPE_IO_APIC:
            if (ioapic_idx < INT_MAX) {
                ++ioapic_idx;
            }
            break;
        case MADT_TYPE_INTERRUPT_OVERRIDE: {
            madt_int_override_t *override = (madt_int_override_t *)sub;
            if (override->source > irq_max) { irq_max = override->source; }
            if ((int)override->gsi > gsi_max) { gsi_max = (int)override->gsi; }
            break;
        }
        case MADT_TYPE_NMI_SOURCE: {
            madt_nmi_t *nmi = (madt_nmi_t *)sub;
            if ((int)nmi->gsi > gsi_max) { gsi_max = (int)nmi->gsi; }
            break;
        }
        default:
            break;
        }
    }

    g_loapic_count = loapic_idx;
    g_ioapic_count = ioapic_idx;
    g_irq_num      = irq_max + 1;
    g_gsi_num      = gsi_max + 1;

    // 分配 Local APIC、IO APIC 结构体的空间
    g_loapics = early_const_alloc(g_loapic_count * sizeof(loapic_t));
    g_ioapics = early_const_alloc(g_ioapic_count * sizeof(ioapic_t));
    memset(g_loapics, 0, g_loapic_count * sizeof(loapic_t));
    memset(g_ioapics, 0, g_ioapic_count * sizeof(ioapic_t));

    // 为 IRQ-GSI 对照表分配空间
    g_irq_to_gsi = early_const_alloc(g_irq_num * sizeof(int));
    g_gsi_to_irq = early_const_alloc(g_gsi_num * sizeof(int));
    g_gsi_flags  = early_const_alloc(g_gsi_num * sizeof(uint16_t));

    // 默认情况下，8259 IRQ 0~15 与 GSI 0~15 对应
    for (int i = 0; i < g_irq_num; ++i) {
        g_irq_to_gsi[i] = i;
    }
    for (int i = 0; i < g_gsi_num; ++i) {
        g_gsi_to_irq[i] = i;
        // TODO 设置默认的 trigger mode、polarity
    }

    // 第二次遍历 MADT，保存 Local APIC、IO APIC 和中断对应表
    loapic_idx = 0;
    ioapic_idx = 0;
    ptr = (uint8_t *)tbl + sizeof(madt_t);
    end = (uint8_t *)tbl + tbl->header.length;
    while (ptr < end) {
        acpi_subtbl_t *sub = (acpi_subtbl_t *)ptr;
        ptr += sub->length;

        switch (sub->type) {
        case MADT_TYPE_LOCAL_APIC: {
            // APIC ID 小于 0xff 的处理器使用这种条目
            madt_loapic_t *loapic = (madt_loapic_t *)sub;
            if (loapic->loapic_flags & 1) {
                // dbg_print("Local APIC id=%d, processor-id=%d, flags=%d\n",
                //         loapic->id, loapic->processor_id, loapic->loapic_flags);
                g_loapics[loapic_idx].apic_id = loapic->id;
                g_loapics[loapic_idx].processor_id = loapic->processor_id;
                ++loapic_idx;
            }
            break;
        }
        case MADT_TYPE_LOCAL_X2APIC: {
            // APIC ID 大于或等于 0xff 的处理器使用这种条目
            madt_lox2apic_t *loapic = (madt_lox2apic_t *)sub;
            if (loapic->loapic_flags & 1) {
                // dbg_print("Local x2APIC id=%d, processor-id=%d, flags=%d\n",
                //         loapic->id, loapic->processor_id, loapic->loapic_flags);
                g_loapics[loapic_idx].apic_id = loapic->id;
                g_loapics[loapic_idx].processor_id = loapic->processor_id;
                ++loapic_idx;
            }
            break;
        }
        case MADT_TYPE_IO_APIC: {
            madt_ioapic_t *ioapic = (madt_ioapic_t *)sub;
            // dbg_print("IO APIC id=%d, addr=0x%x, gsi-start=%d\n",
            //         ioapic->id, ioapic->address, ioapic->gsi_base);
            g_ioapics[ioapic_idx].apic_id = ioapic->id;
            g_ioapics[ioapic_idx].addr = ioapic->address;
            g_ioapics[ioapic_idx].gsi_base = ioapic->gsi_base;
            ++ioapic_idx;
            break;
        }
        case MADT_TYPE_INTERRUPT_OVERRIDE: {
            // 将某个 8259 IRQ 映射到不同的 GSI，也可能编号不变，触发条件改变
            madt_int_override_t *override = (madt_int_override_t *)sub;
            // dbg_print("Int override irq=%d, gsi=%d, ", override->source, override->gsi);
            // show_mps_inti_flags(override->inti_flags);
            g_irq_to_gsi[override->source] = override->gsi;
            g_gsi_to_irq[override->gsi] = override->source;
            g_gsi_flags [override->gsi] = override->inti_flags;
            break;
        }
        default:
            break;
        }
    }
    ASSERT(loapic_idx == g_loapic_count);
    ASSERT(ioapic_idx == g_ioapic_count);

    // 第三次遍历 MADT，将 NMI 信息记录在各 APIC 结构体中
    // 被 NMI 占用的引脚不能被 OS 使用
    // APIC 产生的 NMI 也会触发 int 2
    ptr = (uint8_t *)tbl + sizeof(madt_t);
    end = (uint8_t *)tbl + tbl->header.length;
    while (ptr < end) {
        acpi_subtbl_t *sub = (acpi_subtbl_t *)ptr;
        ptr += sub->length;
        switch (sub->type) {
        case MADT_TYPE_NMI_SOURCE: {
            madt_nmi_t *nmi = (madt_nmi_t *)sub;
            // dbg_print("NMI gsi=%d, ", nmi->gsi);
            // show_mps_inti_flags(nmi->inti_flags);
            // TODO 需要知道这个 IO APIC 对应的 GSI 数量
            // 或者标记在 gsi2irq 数组中
            for (int i = 0; i < g_ioapic_count; ++i) {
                int offset = nmi->gsi - g_ioapics[i].gsi_base;
                if ((offset >= 0) && (offset < 32)) {
                    g_ioapics[i].nmi_mask |= 1 << offset;
                    break;
                }
            }
            break;
        }
        case MADT_TYPE_LOCAL_APIC_NMI: {
            madt_loapic_nmi_t *nmi = (madt_loapic_nmi_t *)sub;
            // dbg_print("NMI loapic[%d].LINT%d, ", nmi->processor_id, nmi->lint);
            // show_mps_inti_flags(nmi->inti_flags);
            for (int i = 0; i < g_loapic_count; ++i) {
                // 如果 processor_id 取值为 0xff，则表示针对所有处理器
                if ((g_loapics[i].apic_id == nmi->processor_id) || (0xff == nmi->processor_id)) {
                    g_loapics[i].nmi_mask |= 1 << nmi->lint;
                }
            }
            break;
        }
        case MADT_TYPE_LOCAL_X2APIC_NMI: {
            madt_lox2apic_nmi_t *nmi = (madt_lox2apic_nmi_t *)sub;
            // dbg_print("NMI lox2apic[%d].LINT%d, ", nmi->processor_id, nmi->lint);
            // show_mps_inti_flags(nmi->inti_flags);
            for (int i = 0; i < g_loapic_count; ++i) {
                // 如果 processor_id 取值为 0xffffffff，则表示针对所有处理器
                if ((g_loapics[i].apic_id == nmi->processor_id) || (0xffffffff == nmi->processor_id)) {
                    g_loapics[i].nmi_mask |= 1 << nmi->lint;
                }
            }
            break;
        }
        default:
            break;
        }
    }
}


//------------------------------------------------------------------------------
// per cpu 数据区
//------------------------------------------------------------------------------

// 记录每个 CPU 的 per cpu 地址偏移量
// 由 arch_mem.c 负责分配 per cpu 空间，并设置这个数组
CONST size_t *g_pcpu_offsets = NULL;
static PCPU_BSS int g_local_cpu_idx; // 当前 CPU 的编号


// 64-bit 模式没有分段功能，却唯独保留了 FS、GS 两个段描述符
// 我们借助 GS.base，可以实现快速访问 this-CPU 数据
INIT_TEXT void gsbase_init(int idx) {
    ASSERT(NULL != g_pcpu_offsets);
    ASSERT(idx < g_loapic_count);
    write_gsbase(g_pcpu_offsets[idx]);
    THISCPU_SET(g_local_cpu_idx, idx);
}

void *pcpu_ptr(int idx, void *ptr) {
    ASSERT(NULL != g_pcpu_offsets);

    return (uint8_t *)ptr + g_pcpu_offsets[idx];
}

// lea 只能获取有效地址，不包括 GS.base 基地址
// 因此计算 thiscpu 指针只能获取 GS.base 再相加
void *this_ptr(void *ptr) {
    ASSERT(NULL != g_pcpu_offsets);

    return (uint8_t *)ptr + read_gsbase();
}


//------------------------------------------------------------------------------
// 实现 arch-API 函数
//------------------------------------------------------------------------------

int64_t get_cpu_count() {
    return g_loapic_count;
}

int get_cpu_index() {
    ASSERT(NULL != g_pcpu_offsets);

    int idx;
    __asm__("movl %%gs:%1, %0" : "=a"(idx) : "m"(g_local_cpu_idx));
    return idx;
}
