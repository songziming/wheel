#include <wheel.h>

#define ZERO_MEMORY 0

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
            ioapic_dev_add((madt_ioapic_t *) sub);
            break;
        case MADT_TYPE_INTERRUPT_OVERRIDE:
            ioapic_int_override((madt_int_override_t *) sub);
            break;
        case MADT_TYPE_LOCAL_APIC:
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
    for (int i = 0; i < cpu_count(); ++i) {
        memcpy(kernel_end, &_percpu_addr, percpu_size);
        kernel_end += percpu_size;
    }

    // page array comes right after percpu area
    kernel_end = (u8 *) ROUND_UP((u64) kernel_end + cpu_count() * percpu_size, 16);
    page_array = (page_t *) kernel_end;
    page_count = 0;

    // walk through the memory layout table, fill invalid entries of page array
    mb_mmap_item_t * map_end = (mb_mmap_item_t *) (mmap_buf + mmap_len);
    for (mb_mmap_item_t * item = (mb_mmap_item_t *) mmap_buf; item < map_end;) {
        pfn_t start = (item->addr + PAGE_SIZE - 1) >> PAGE_SHIFT;
        pfn_t end   = (item->addr + item->len)     >> PAGE_SHIFT;
        if ((start < end) && (MB_MEMORY_AVAILABLE == item->type)) {
            for (; page_count < start; ++page_count) {
                page_array[page_count].type = PT_INVALID;
            }
            for (; page_count < end; ++page_count) {
                page_array[page_count].type = PT_KERNEL;
            }
        }
        item = (mb_mmap_item_t *) ((u64) item + item->size + sizeof(item->size));
    }

    // walk through the table again, add usable ranges to page frame allocator
    u64 p_end = ROUND_UP(virt_to_phys(&page_array[page_count]), PAGE_SIZE);
    for (mb_mmap_item_t * item = (mb_mmap_item_t *) mmap_buf; item < map_end;) {
        u64 start = ROUND_UP(item->addr, PAGE_SIZE);
        u64 end   = ROUND_DOWN(item->addr + item->len, PAGE_SIZE);
        if ((start < end) && (MB_MEMORY_RESERVED == item->type)) {
            // TODO: map this range into kernel space
            // dbg_print("[+] mmio 0x%08llx-0x%08llx.\n", start, end - 1);
        }
        if ((start < end) && (MB_MEMORY_AVAILABLE == item->type)) {
            if (start < KERNEL_LMA) {
                // dbg_print("[+] ram  0x%016llx-0x%016llx\n", start, MIN(KERNEL_LMA, end) - 1);
#if ZERO_MEMORY
                memset(phys_to_virt(start), 0, MIN(KERNEL_LMA, end) - start);
#endif
                page_range_free(start, MIN(KERNEL_LMA, end));
            }
            if (p_end < end) {
                // dbg_print("[+] ram  0x%016llx-0x%016llx\n", MAX(start, p_end), end - 1);
#if ZERO_MEMORY
                memset(phys_to_virt(MAX(start, p_end)), 0, end - MAX(start, p_end));
#endif
                page_range_free(MAX(start, p_end), end);
            }
        }
        item = (mb_mmap_item_t *) ((u64) item + item->size + sizeof(item->size));
    }
}

//------------------------------------------------------------------------------
// pre-kernel initialization

static void root_proc();

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
    write_gsbase(percpu_base);

    // init tss and interrupt, both require percpu-var
    tss_init();
    int_init();

    // init interrupt controller
    ioapic_all_init();
    loapic_dev_init();

    // prepare and switch to kernel space
    mmu_lib_init();
    kernel_ctx_init();

    // init kernel memory allocator
    kmem_lib_init();
    work_lib_init();
    sched_lib_init();

    // temporary tcb to hold saved registers
    task_t dummy = {
        .priority = PRIORITY_IDLE + 1
    };
    thiscpu_var(tid_prev) = &dummy;

    // start root task
    dbg_print("[!] cpu %02d started.\n", cpu_activated++);
    task_resume(task_create(0, root_proc, 0,0,0,0));

    dbg_print("YOU SHALL NOT SEE THIS LINE!\n");
    while (1) {}
}

__INIT __NORETURN void sys_init_ap() {
    // prepare essential cpu features
    cpu_init();
    gdt_init();
    idt_init();

    // thiscpu and tss
    write_gsbase(percpu_base + cpu_activated * percpu_size);
    tss_init();

    // interrupt controller
    loapic_dev_init();

    // switch to kernel space
    mmu_lib_init();
    kernel_ctx_load();

    // temporary tcb to hold saved registers
    task_t dummy = {
        .priority = PRIORITY_IDLE + 1
    };
    thiscpu_var(tid_prev) = &dummy;

    // start idle task
    dbg_print("[!] cpu %02d started.\n", cpu_activated++);
    sched_yield();

    dbg_print("YOU SHALL NOT SEE THIS LINE!\n");
    while (1) {}
}

//------------------------------------------------------------------------------
// post-kernel initialization

// defined in `layout.ld`
extern u8 _trampoline_addr;
extern u8 _trampoline_end;
extern u8 _init_end;

// in `core/shell.c`
extern void shell_lib_init();
void test();

static void root_proc() {
    // copy trampoline code to 0x7c000
    u8 * src = (u8 *) &_trampoline_addr;
    u8 * dst = (u8 *) phys_to_virt(0x7c000);
    u64  len = (u64) (&_trampoline_end - &_trampoline_addr);
    memcpy(dst, src, len);

    for (int i = 1; i < cpu_count(); ++i) {
        loapic_emit_init(i);                            // send INIT
        tick_delay(MAX(CFG_SYS_CLOCK_RATE/100, 1));     // wait for 10ms
        loapic_emit_sipi(i, 0x7c);                      // send SIPI
        tick_delay(MAX(CFG_SYS_CLOCK_RATE/1000, 1));    // wait for 1ms
        loapic_emit_sipi(i, 0x7c);                      // send SIPI again
        tick_delay(MAX(CFG_SYS_CLOCK_RATE/1000, 1));    // wait for 1ms

        // same boot stack is used, have to start cpus one-by-one
        while ((percpu_var(i, tid_prev) == NULL) ||
               (percpu_var(i, tid_prev)->priority > PRIORITY_IDLE)) {
            tick_delay(10);
        }
    }

    // starting other kernel components
    tty_dev_init();
    ps2kbd_dev_init();

    // reclaim init section memory
    usize init_addr = KERNEL_LMA;
    usize init_end  = ROUND_UP(virt_to_phys(&_init_end), PAGE_SIZE);
    // dbg_print("[+] free 0x%016llx~0x%016llx (init)\n", init_addr, init_end);
#if ZERO_MEMORY
    memset((void *) KERNEL_VMA, 0, init_end - init_addr);
#endif
    page_range_free(init_addr, init_end);
    mmu_unmap(mmu_ctx_get(), KERNEL_VMA, (init_end - init_addr) >> PAGE_SHIFT);
    tlb_flush(KERNEL_VMA, (init_end - init_addr) >> PAGE_SHIFT);

    // run test program
    test();

    // start kernel shell
    shell_lib_init();

    task_exit();
    dbg_print("you shall not see this line!\n");
}

//------------------------------------------------------------------------------
// (test) block device interface

// like linux bio
typedef struct blkdev {
    int ref_count;
    usize sec_size;
} blkdev_t;

//------------------------------------------------------------------------------
// (test) PCI

extern u32  pci_read (u8 bus, u8 dev, u8 func, u8 reg);
extern void pci_write(u8 bus, u8 dev, u8 func, u8 reg, u32 data);
extern void pci_lib_init();

//------------------------------------------------------------------------------
// (test) IDE driver

void ide_probe(u8 bus, u8 dev, u8 func) {
    u32 reg0 = pci_read(bus, dev, func, 0);
    u32 reg2 = pci_read(bus, dev, func, 8);
    u16 vendor =  reg0        & 0xffff;
    u16 device = (reg0 >> 16) & 0xffff;
    u16 ccode  = (reg2 >> 16) & 0xffff;   // base and sub class code
    u8  prog   = (reg2 >>  8) & 0xff;     // programming interface

    u32 regf = pci_read(bus, dev, func, 0x3c);
    pci_write(bus, dev, func, 0x3c, (regf & ~0xff) | 0xfe); // update int line

    if (0xfe == (pci_read(bus, dev, func, 0x3c) & 0xff)) {
        // this device needs an IRQ assignment
        dbg_print("ide needs irq assignment.\n");
    } else {
        if ((0x80 == prog) || (0x8a == prog)) {
            // this is parallel IDE controller, uses IRQ 14 and 15
            dbg_print("this is parallel ide controller.\n");
        }
    }
}

//------------------------------------------------------------------------------
// fat file system

// FAT views the storage media as a flat array of clusters

// BIOS parameter block, all numbers are little-endian
typedef struct bpb {
    u8  jmp[3];
    u8  oem[8]; // recommended "MSWIN4.1"
    u16 bytes_per_sector;
    u8  sectors_per_cluster;
    u16 reserved_sectors;
    u8  fat_count;
    u16 dir_entry_count;
    u16 sector_count;       // 0 means more than 65535
    u8  media_descriptor_type;
    u16 sectors_per_fat;    // FAT 12/16 only
    u16 sectors_per_track;
    u16 head_count;         // number of heads or sides
    u32 hidden_sector_count;
    u32 large_sector_count; // valid if `sector_count` == 0
} __PACKED bpb_t;

// extended boot record, comes right after bpb
typedef struct ebr {
    u8  drive_number;
    u8  winnt_flag;
    u8  signature;          // must be 0x28 or 0x29
    u32 vol_id;
    u8  vol_label[11];      // padded with string
    u8  sys_id[8];
} __PACKED ebr_t;

//------------------------------------------------------------------------------
// test driver

extern void ata_init();

void test() {
    pci_lib_init();
    ata_init();
}
