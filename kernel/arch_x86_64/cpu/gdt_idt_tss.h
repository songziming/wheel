#ifndef ARCH_X86_64_CPU_GDT_IDT_TSS_H
#define ARCH_X86_64_CPU_GDT_IDT_TSS_H

#include <wheel.h>

INIT_TEXT void gdt_init();
INIT_TEXT void gdt_load();

INIT_TEXT void idt_init();
INIT_TEXT void idt_load();
INIT_TEXT void idt_set_isr(uint8_t vec, uint64_t isr, int dpl);
INIT_TEXT void idt_set_ist(uint8_t vec, int idx);

INIT_TEXT void thistss_init_load();
INIT_TEXT void thistss_set_ist(int ist, uint64_t addr);
void thistss_set_rsp(int ring, uint64_t addr);

#endif // ARCH_X86_64_CPU_GDT_IDT_TSS_H
