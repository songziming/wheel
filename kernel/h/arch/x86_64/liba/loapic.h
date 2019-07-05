#ifndef ARCH_X86_64_LIBA_LOAPIC_H
#define ARCH_X86_64_LIBA_LOAPIC_H

#include <base.h>

typedef struct madt_loapic     madt_loapic_t;
typedef struct madt_loapic_mni madt_loapic_mni_t;

// range 0xf0~0xf7 are for all kinds of IPI
#define VECNUM_RESCHED  0xf0
#define VECNUM_FLUSHMMU 0xf1
#define VECNUM_TIMER    0xfc
#define VECNUM_THERMAL  0xfd
#define VECNUM_ERROR    0xfe
#define VECNUM_SPURIOUS 0xff

extern u8   loapic_get_id  ();
extern void loapic_send_eoi();
extern void loapic_emit_ipi(int cpu, int vec);

// requires: nothing
extern __INIT void loapic_override(u64 addr);
extern __INIT void loapic_dev_add (madt_loapic_t * tbl);
extern __INIT void loapic_set_nmi (madt_loapic_mni_t * tbl);
extern __INIT void loapic_dev_init();
extern __INIT void loapic_emit_init(int cpu);
extern __INIT void loapic_emit_sipi(int cpu, int vec);

#endif // ARCH_X86_64_LIBA_LOAPIC_H
