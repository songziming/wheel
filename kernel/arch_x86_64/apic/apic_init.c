#include "apic.h"
#include <early_alloc.h>
#include <debug.h>



CONST int    g_loapic_num;
CONST size_t g_loapic_addr;
CONST loapic_t *g_loapics;

CONST int g_ioapic_num;
CONST ioapic_t *g_ioapics;



// 最大的 apicid，能否使用 4bit 表示，决定了我们能否使用 x2APIC
// 如果无法对单个 CPU 发送 IPI，就必须 remap
static INIT_DATA uint32_t g_max_apicid = 0;


// 判断是否需要配置 interrupt remapper
// TODO 该函数应该放在 apic_init.c
INIT_TEXT int need_int_remap() {
    uint32_t max_apicid = 0;
    for (int i = 0; i < g_loapic_num; ++i) {
    }

    char not_physical = 0;  // 无法使用 physical 模式定位每个 CPU
    char not_logical = 0;   // 无法使用 logical 模式定位每个 CPU
    for (int i = 0; i < g_loapic_num; ++i) {
        uint32_t apicid = g_loapics[i].apic_id;
        if (g_max_apicid < apicid) {
            g_max_apicid = apicid;
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

    // cpu-id 超过了 255，无法使用 8-bit 表示
    if (g_max_apicid >= 255) {
        return 1;
    }

    // TODO 根据 CPU 数量，决定 IO APIC 使用 physical 还是 logical
    // TODO 可以增加一个函数，输入 CPU 编号，返回 8-bit dest 内容

    // 两种模式都无法表示，才说明必须 remap interrupts
    return not_physical & not_logical;
}


// TODO move to standalone file
INIT_TEXT void parse_madt(madt_t *madt) {
    g_loapic_addr = (size_t)madt->loapic_addr;
    logk("local apic base = %zx\n", g_loapic_addr);

    // 统计 local apic、io apic 个数，irq-gsi 映射表长度
    g_loapic_num = 0;
    g_ioapic_num = 0;
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
        }
    }

    g_loapics = early_alloc_ro(g_loapic_num * sizeof(loapic_t));
    g_ioapics = early_alloc_ro(g_ioapic_num * sizeof(ioapic_t));

    // 第二次遍历，记录信息
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
            // ioapic_parse(&g_ioapics[io_idx++], (madt_ioapic_t*)sub);
            break;
        case MADT_TYPE_INTERRUPT_OVERRIDE:
            // override_int((madt_int_override_t*)sub);
            break;
        default:
            break;
        }
    }
    ASSERT(lo_idx == g_loapic_num);
    // ASSERT(io_idx == g_ioapic_num);
}
