// 控制 CPU 相关表结构

#include <arch_cpu.h>
#include <wheel.h>



// arch_entries.S
extern uint64_t isr_entries[256];




typedef struct idt_ent {
    uint16_t offset_low;
    uint16_t selector;
    uint16_t attr;
    uint16_t offset_mid;
    uint32_t offset_high;
    uint32_t reserved;
} PACKED idt_ent_t;

static CONST idt_ent_t g_idt[256];


INIT_TEXT void idt_init() {
    for (int i = 0; i < 256; ++i) {
        g_idt[i].attr        = 0x8e00;  // dpl = 0
        g_idt[i].selector    = 0x08;    // 内核代码段
        g_idt[i].offset_low  =  isr_entries[i]        & 0xffff;
        g_idt[i].offset_mid  = (isr_entries[i] >> 16) & 0xffff;
        g_idt[i].offset_high = (isr_entries[i] >> 32) & 0xffffffff;
        g_idt[i].reserved    = 0;
    }
}

