#include <stdint.h>

extern "C" {

#include <arch_api.h>

// layout.ld
char _init_end;
char _text_addr, _text_end;
char _rodata_addr;
char _data_addr;
char _percpu_addr, _percpu_data_end, _percpu_bss_end;

// cpu/gdt_idt_tss.S
void load_gdtr() {}
void load_idtr() {}
void load_tr() {}

// arch_entries.S
uint64_t isr_entries[1];
// void task_entry() {}
void syscall_entry() {}
void arch_task_switch() {}

size_t arch_cacheline_size() {
    return 64;
}

} // extern "C"
