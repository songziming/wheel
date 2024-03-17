// 处理未定义行为（-fsanitize=undefined）

#include <def.h>
#include <debug.h>

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

static void log_location(source_location_t *loc) {
    klog("location: %s:%d,%d\n", loc->file, loc->line, loc->column);
}




const char *TYPE_CHECK_KINFS[] = {
    "load of",
    "store to",
    "reference binding to",
    "member access within",
    "member call on",
    "constructor call on",
    "downcast of",
    "downcast of",
    "upcast of",
    "cast to virtual base of",
};

static void type_mismatch_common(source_location_t *loc, type_descriptor_t *type, uintptr_t alignment, uint8_t kind, uintptr_t ptr) {
    if (0 == ptr) {
        klog("\nubsan: null pointer access\n");
    } else if (alignment && (ptr & (alignment - 1))) {
        klog("\nubsan: unaligned memory access, alignment=%lx, ptr=%lx\n", alignment, ptr);
    } else {
        klog("\nubsan: %s addr %lx with insufficient space for object of type %s\n",
            TYPE_CHECK_KINFS[kind], ptr, type->name);
    }
    log_location(loc);
    print_stacktrace();
    // emu_exit(1);
}

void __ubsan_handle_type_mismatch(type_mismatch_data_t *data, uintptr_t ptr) {
    type_mismatch_common(&data->location, data->type, data->alignment, data->type_check_kind, ptr);
}

void __ubsan_handle_type_mismatch_v1(type_mismatch_data_v1_t *data, uintptr_t ptr) {
    type_mismatch_common(&data->location, data->type, 1UL << data->log_alignment, data->type_check_kind, ptr);
}



void __ubsan_handle_add_overflow(source_location_t *loc, UNUSED uintptr_t a, UNUSED uintptr_t b) {
    klog("\nubsan: addition overflow\n");
    log_location(loc);
    print_stacktrace();
    // emu_exit(1);
}

void __ubsan_handle_sub_overflow(source_location_t *loc, UNUSED uintptr_t a, UNUSED uintptr_t b) {
    klog("\nubsan: subtraction overflow\n");
    log_location(loc);
    print_stacktrace();
    // emu_exit(1);
}

void __ubsan_handle_mul_overflow(source_location_t *loc, UNUSED uintptr_t a, UNUSED uintptr_t b) {
    klog("\nubsan: multiplication overflow\n");
    log_location(loc);
    print_stacktrace();
    // emu_exit(1);
}

void __ubsan_handle_negate_overflow(source_location_t *loc, UNUSED uintptr_t a, UNUSED uintptr_t b) {
    klog("\nubsan: negation overflow\n");
    log_location(loc);
    print_stacktrace();
    // emu_exit(1);
}

void __ubsan_handle_divrem_overflow(source_location_t *loc, UNUSED uintptr_t a, UNUSED uintptr_t b) {
    klog("\nubsan: division remainder overflow\n");
    log_location(loc);
    print_stacktrace();
    // emu_exit(1);
}

void __ubsan_handle_pointer_overflow(source_location_t *loc, UNUSED uintptr_t a, UNUSED uintptr_t b) {
    klog("\nubsan: pointer overflow\n");
    log_location(loc);
    print_stacktrace();
    // emu_exit(1);
}


void __ubsan_handle_out_of_bounds(out_of_bounds_data_t *data, uintptr_t idx) {
    klog("\nubsan: index %ld out of bound\n", idx);
    log_location(&data->location);
    print_stacktrace();
    // emu_exit(1);
}

void __ubsan_handle_shift_out_of_bounds(shift_data_t *data, UNUSED uintptr_t lhs, UNUSED uintptr_t rhs) {
    klog("\nubsan: shift out of bound\n");
    log_location(&data->location);
    print_stacktrace();
    // emu_exit(1);
}

void __ubsan_handle_function_type_mismatch(source_location_t *loc) {
    klog("\nubsan: function type mismatch\n");
    log_location(loc);
    print_stacktrace();
    // emu_exit(1);
}

void __ubsan_handle_invalid_builtin(source_location_t *loc) {
    klog("\nubsan: invalid builtin\n");
    log_location(loc);
    print_stacktrace();
    // emu_exit(1);
}
