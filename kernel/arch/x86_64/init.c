#include <wheel.h>

//------------------------------------------------------------------------------
// parse madt table, find all local apic and io apic

static __INIT void parse_madt(madt_t * tbl) {
    u8 * end = (u8 *) tbl + tbl->header.length;
    u8 * p   = (u8 *) tbl + sizeof(madt_t);

    cpu_installed = 0;
    cpu_activated = 0;

    loapic_override(tbl->loapic_addr);
    while (p < end) {
        acpi_subtbl_t * sub = (acpi_subtbl_t *) p;
        switch (sub->type) {
        case MADT_TYPE_IO_APIC:
            // dbg_print("got io apic.\n");
            ioapic_dev_add((madt_ioapic_t *) sub);
            break;
        case MADT_TYPE_INTERRUPT_OVERRIDE:
            ioapic_int_override((madt_int_override_t *) sub);
            break;
        case MADT_TYPE_LOCAL_APIC:
            // dbg_print("got local apic.\n");
            loapic_dev_add((madt_loapic_t *) sub);
            break;
        case MADT_TYPE_LOCAL_APIC_OVERRIDE:
            loapic_override(((madt_loapic_override_t *) sub)->address);
            break;
        case MADT_TYPE_LOCAL_APIC_NMI:
            loapic_set_nmi((madt_loapic_mni_t *) sub);
            break;
        default:
            dbg_print("madt entry type %d not known!\n", sub->type);
            break;
        }
        p += sub->length;
    }
}



//------------------------------------------------------------------------------
// pre-kernel initialization

__INIT __NORETURN void sys_init_bsp(u32 ebx) {
    // init serial and console device
    serial_dev_init();
    console_dev_init();

    // enable debug output
    dbg_trace_hook = dbg_trace_here;
    dbg_write_hook = dbg_write_text;
    dbg_print("wheel operating system starting up.\n");

    // backup multiboot info
    mb_info_t * mbi = (mb_info_t *) allot_temporary(sizeof(mb_info_t));
    memcpy(mbi, phys_to_virt(ebx), sizeof(mb_info_t));

    // backup memory map
    u8 * mmap_buff = (u8 *) allot_temporary(mbi->mmap_length);
    memcpy(mmap_buff, phys_to_virt(mbi->mmap_addr), mbi->mmap_length);

    // get multi-processor info
    acpi_tbl_init();
    parse_madt(acpi_madt);

    dbg_print("we got %d cpu(s) installed.\n", cpu_installed);

    char * s = (char *) phys_to_virt(mbi->boot_loader_name);
    dbg_print("loaded by: %s.\n", s);

    while (1) {}
}

__INIT __NORETURN void sys_init_ap() {
    while (1) {}
}
