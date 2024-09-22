#ifndef LOAPIC_H
#define LOAPIC_H

#include <common.h>

typedef struct loapic {
    uint32_t apic_id;
    uint32_t processor_id;
    uint32_t flags;

    uint16_t cluster_id;
    uint16_t logical_id;
} loapic_t;

#endif // LOAPIC_H
