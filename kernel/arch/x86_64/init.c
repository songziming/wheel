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
// parse physical memory map

// defined in `layout.ld`
extern u8 _percpu_addr;
extern u8 _percpu_end;

static __INIT void parse_mmap(u8 * mmap_buf, u32 mmap_len) {
    // reserve space for percpu sections
    u8 * kernel_end = (u8 *) ROUND_UP(allot_permanent(0), 64);
    percpu_base = (u64) (kernel_end - &_percpu_addr);
    percpu_size = ROUND_UP((u64) (&_percpu_end - &_percpu_addr), 64);
    for (int i = 0; i < cpu_installed; ++i) {
        memcpy(kernel_end, &_percpu_addr, percpu_size);
        kernel_end += percpu_size;
    }

    // page array comes right after percpu area
    kernel_end = (u8 *) ROUND_UP((u64) kernel_end + cpu_installed * percpu_size, 16);
    page_array = (page_t *) kernel_end;
    page_count = 0;
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

    // copy and regist kernel symbol table
    u8 * shdrs = (u8 *) phys_to_virt(mbi->elf.addr);
    for (u32 i = 1; i < mbi->elf.num; ++i) {
        elf64_shdr_t * sym = (elf64_shdr_t *) (shdrs + mbi->elf.size * i);
        elf64_shdr_t * str = (elf64_shdr_t *) (shdrs + mbi->elf.size * sym->sh_link);
        if ((SHT_SYMTAB == sym->sh_type) && (SHT_STRTAB == str->sh_type)) {
            u8 * symtbl = (u8 *) allot_permanent(sym->sh_size);
            u8 * strtbl = (u8 *) allot_permanent(str->sh_size);
            memcpy(symtbl, phys_to_virt(sym->sh_addr), sym->sh_size);
            memcpy(strtbl, phys_to_virt(str->sh_addr), str->sh_size);
            regist_symtbl(symtbl, sym->sh_size);
            regist_strtbl(strtbl, str->sh_size);
            break;
        }
    }

    // get multi-processor info
    acpi_tbl_init();
    parse_madt(acpi_madt);

    // prepare essential cpu features
    cpu_init();
    gdt_init();
    idt_init();

    // init page allocator and percpu-var support
    page_lib_init();
    parse_mmap(mmap_buff, mbi->mmap_length);

    // init tss and interrupt, both require percpu-var
    tss_init();
    int_init();

    // init interrupt controller
    ioapic_all_init();
    loapic_dev_init();

    char * s = (char *) phys_to_virt(mbi->boot_loader_name);
    dbg_print("bootloaded name: %s.\n", s);
    dbg_print("processor count: %d.\n", cpu_installed);
    dbg_trace_here();

    while (1) {}
}

__INIT __NORETURN void sys_init_ap() {
    while (1) {}
}
