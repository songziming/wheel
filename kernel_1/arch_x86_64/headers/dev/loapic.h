#ifndef DEV_LOAPIC_H
#define DEV_LOAPIC_H

#include <base.h>


// 中断向量号
#define VEC_LOAPIC_SPURIOUS 0x4f
#define VEC_LOAPIC_TIMER    0xfc


INIT_TEXT void loapic_init();

void loapic_set_freq(int freq);
// void loapic_busywait(int ms);
void loapic_emit_init(uint32_t id);
void loapic_emit_startup(uint32_t id, int vec);

void loapic_busywait(uint64_t us);
void tsc_busywait(uint64_t us);

#endif // DEV_LOAPIC_H
