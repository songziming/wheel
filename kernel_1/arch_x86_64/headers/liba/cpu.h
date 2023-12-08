#ifndef LIBA_CPU_H
#define LIBA_CPU_H

#include <base.h>

INIT_TEXT void cpu_feat_init();

INIT_TEXT void gdt_init_load();
INIT_TEXT void tss_init_load();
void tss_set_rsp0(uint64_t rsp0);

INIT_TEXT void idt_init();
INIT_TEXT void idt_load();

#endif // LIBA_CPU_H
