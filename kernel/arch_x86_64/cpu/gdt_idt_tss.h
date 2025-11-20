#ifndef ARCH_X86_64_CPU_GDT_IDT_TSS_H
#define ARCH_X86_64_CPU_GDT_IDT_TSS_H

#include <wheel.h>

INIT_TEXT void gdt_init();
INIT_TEXT void gdt_load();

INIT_TEXT void idt_init();
INIT_TEXT void idt_load();
INIT_TEXT void idt_set_ist(uint8_t vec, unsigned idx);

INIT_TEXT void tss_init_load();
INIT_TEXT void tss_set_ist(int cpu, int idx, uint64_t addr);

void tss_set_rsp(int ring, uint64_t addr);

#endif // ARCH_X86_64_CPU_GDT_IDT_TSS_H
