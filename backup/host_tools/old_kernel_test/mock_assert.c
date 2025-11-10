// 替代内核代码的原本实现

#include <stdio.h>
#include <devices/acpi.h>

// #include <library/debug.h>


int cpu_int_depth() {
    return -123;
}

acpi_tbl_t *acpi_table_find(const char sig[4], int idx) {
    fprintf(stdout, "calling on table %.4s\n", sig);
    return NULL;
}


void assertion_fail(const char *file, int line, const char *func) {
    fprintf(stderr, "assert fail in test program!\n");
    fprintf(stderr, "fail on %s %s:%d\n", func, file, line);
}
