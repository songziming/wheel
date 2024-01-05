#ifndef CPU_GDT_IDT_TSS_H
#define CPU_GDT_IDT_TSS_H

#include <def.h>

INIT_TEXT void gdt_init();
INIT_TEXT void gdt_load();

INIT_TEXT void idt_init();
INIT_TEXT void idt_load();
INIT_TEXT void idt_set_ist(int vec, int idx);

INIT_TEXT void tss_init_load();
INIT_TEXT void tss_set_rsp(int cpu, int idx, uint64_t addr);
INIT_TEXT void tss_set_ist(int cpu, int idx, uint64_t addr);

#endif // CPU_GDT_IDT_TSS_H
