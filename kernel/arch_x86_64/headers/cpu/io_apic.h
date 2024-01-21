#ifndef IO_APIC_H
#define IO_APIC_H

#include <def.h>
#include <arch_smp.h>

void io_apic_send_eoi(ioapic_t *io, uint8_t vec);

INIT_TEXT void io_apic_init_all();

#endif // IO_APIC_H
