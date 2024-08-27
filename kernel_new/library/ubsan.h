#ifndef UBSAN_H
#define UBSAN_H

#include <common.h>

typedef struct source_location {
    const char *file;
    uint32_t line;
    uint32_t column;
} source_location_t;

typedef struct type_descriptor {
    uint16_t kind;
    uint16_t info;
    char name[];
} type_descriptor_t;

typedef struct type_mismatch_data {
    source_location_t location;
    type_descriptor_t *type;
    uintptr_t alignment;
    uint8_t type_check_kind;
} type_mismatch_data_t;

typedef struct type_mismatch_data_v1 {
    source_location_t location;
    type_descriptor_t *type;
    uint8_t log_alignment;
    uint8_t type_check_kind;
} type_mismatch_data_v1_t;

typedef struct out_of_bounds_data {
    source_location_t location;
    type_descriptor_t *arr_type;
    type_descriptor_t *idx_type;
} out_of_bounds_data_t;

typedef struct shift_data {
    source_location_t location;
    type_descriptor_t *lhs_type;
    type_descriptor_t *rhs_type;
} shift_data_t;

typedef struct vla_bound_data {
    source_location_t location;
    type_descriptor_t *type;
} vla_bound_data_t;

#endif // UBSAN_H
