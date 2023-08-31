#include <arch_smp.h>
#include <debug.h>


#define SHOW_MADT_DETAIL 1

#if defined(DEBUG) && SHOW_MADT_DETAIL
    #define MADT_PRINT(...) dbg_print(__VA_ARGS__)
#else
    #define MADT_PRINT(...)
#endif


static CONST size_t   g_loapic_addr;
static CONST uint32_t g_loapic_num; // x2APIC 最多允许 4G-1 个节点
static CONST int      g_ioapic_num;


// 解析 MADT，记录 Local APIC、IO APIC
INIT_TEXT void parse_madt(madt_t *madt) {
    g_loapic_addr = madt->loapic_addr;
    g_loapic_num = 0;
    g_ioapic_num = 0;

    // 记录 irq-gsi 映射表的大小
    int irq_max = 0;
    int gsi_max = 0;

    // 第一次遍历 MADT，统计 Local APIC、IO APIC 的数量
    MADT_PRINT("MADT content:\n");
    uint8_t *ptr = (uint8_t *)madt + sizeof(madt_t);
    uint8_t *end = (uint8_t *)madt + madt->header.length;
    while (ptr < end) {
        acpi_subtbl_t *sub = (acpi_subtbl_t *)ptr;
        ptr += sub->length;

        switch (sub->type) {
        case MADT_TYPE_LOCAL_APIC_OVERRIDE:
            g_loapic_addr = ((madt_loapic_override_t *)sub)->address;
            break;
        case MADT_TYPE_LOCAL_APIC: {
            madt_loapic_t *lo = (madt_loapic_t *)sub;
            MADT_PRINT("  - LOCAL_APIC id=%d, proc-id=%d, flags=%x\n",
                lo->id, lo->processor_id, lo->loapic_flags);
            if (1 & lo->loapic_flags) {
                ++g_loapic_num;
            }
            break;
        }
        case MADT_TYPE_LOCAL_X2APIC: {
            madt_lox2apic_t *lo = (madt_lox2apic_t *)sub;
            MADT_PRINT("  - LOCAL_X2APIC id=%d, proc-id=%d, flags=%x\n",
                lo->id, lo->processor_id, lo->loapic_flags);
            if (1 & lo->loapic_flags) {
                ++g_loapic_num;
            }
            break;
        }
        case MADT_TYPE_IO_APIC:
            ++g_loapic_num;
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

    // TODO 为 Local APIC、IO APIC 设备分配空间

    // TODO 再次遍历 MADT，创建设备
}
