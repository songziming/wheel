#include <arch_smp.h>
#include <debug.h>
#include <arch_api.h>


#define SHOW_MADT_DETAIL 1

#if defined(DEBUG) && SHOW_MADT_DETAIL
    #define MADT_PRINT(...) dbg_print(__VA_ARGS__)
#else
    #define MADT_PRINT(...)
#endif


static CONST size_t   g_loapic_addr;
static CONST uint32_t g_loapic_num = 0; // x2APIC 最多允许 4G-1 个节点
static CONST int      g_ioapic_num = 0;


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

    const size_t LEN = madt->header.length;

    g_loapic_addr = madt->loapic_addr;

    // 第一次遍历 MADT，统计 Local APIC、IO APIC 的数量
    // 以及 irq-gsi 映射表需要的大小
    for (size_t i = sizeof(madt); i < LEN;) {
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
    uint32_t loapic_idx = 0;
    int      ioapic_idx = 0;
    MADT_PRINT("MADT content:\n");
    for (size_t i = sizeof(madt); i < LEN;) {
        acpi_subtbl_t *sub = (acpi_subtbl_t *)((size_t)madt + i);
        i += sub->length;

        switch (sub->type) {
        case MADT_TYPE_LOCAL_APIC: {
            madt_loapic_t *lo = (madt_loapic_t *)sub;
            MADT_PRINT("  - LOCAL_APIC id=%d, proc-id=%d, flags=%x\n",
                lo->id, lo->processor_id, lo->loapic_flags);
            if (1 & lo->loapic_flags) {
                ++loapic_idx;
            }
            break;
        }
        case MADT_TYPE_LOCAL_X2APIC: {
            madt_lox2apic_t *lo = (madt_lox2apic_t *)sub;
            MADT_PRINT("  - LOCAL_X2APIC id=%d, proc-id=%d, flags=%x\n",
                lo->id, lo->processor_id, lo->loapic_flags);
            if (1 & lo->loapic_flags) {
                ++loapic_idx;
            }
            break;
        }
        case MADT_TYPE_IO_APIC: {
            madt_ioapic_t *io = (madt_ioapic_t *)sub;
            MADT_PRINT("  - IO_APIC id=%d, gsi-base=%d\n", io->id, io->gsi_base);
            ++ioapic_idx;
            break;
        }
        case MADT_TYPE_INTERRUPT_OVERRIDE: {
            madt_int_override_t *override = (madt_int_override_t *)sub;
            MADT_PRINT("  - INT_OVERRIDE irq=%d, gsi=%u\n", override->source, override->gsi);
            break;
        }
        case MADT_TYPE_NMI_SOURCE: {
            madt_nmi_t *nmi = (madt_nmi_t *)sub;
            MADT_PRINT("  - NMI_SOURCE gsi=%u\n", nmi->gsi);
            break;
        }
        default:
            break;
        }
    }

    ASSERT(loapic_idx == g_loapic_num);
    ASSERT(ioapic_idx == g_ioapic_num);
}
