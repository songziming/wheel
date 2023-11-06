#ifndef ARCH_X86_64_LIBA_IOAPIC_H
#define ARCH_X86_64_LIBA_IOAPIC_H

#include <base.h>

typedef struct madt_ioapic       madr_ioapic_t;
typedef struct madt_int_override madt_int_override_t;

extern int  ioapic_irq_to_gsi(int irq);
extern int  ioapic_gsi_to_vec(int gsi);
extern void ioapic_gsi_mask  (int gsi);
extern void ioapic_gsi_unmask(int gsi);

// requires: nothing
extern __INIT void ioapic_dev_add(madt_ioapic_t * tbl);
extern __INIT void ioapic_int_override(madt_int_override_t * tbl);
extern __INIT void ioapic_all_init();

#endif // ARCH_X86_64_LIBA_IOAPIC_H
