#include <stdint.h>

extern "C" {

#include <arch_api.h>

// layout.ld
char _init_end;
char _text_addr, _text_end;
char _rodata_addr;
char _data_addr;
char _percpu_addr, _percpu_data_end, _percpu_bss_end;
char _kcmd_addr, _kcmd_end;

// cpu/gdt_idt_tss.S
void load_gdtr() {}
void load_idtr() {}
void load_tr() {}

// arch_entries.S
uint64_t isr_entries[1];
// void task_entry() {}
void syscall_entry() {}
void arch_task_switch() {}
void arch_entry_ring3(size_t entry, size_t stack_top) {
    (void)entry;
    (void)stack_top;
}

size_t arch_cacheline_size() {
    return 64;
}

} // extern "C"
