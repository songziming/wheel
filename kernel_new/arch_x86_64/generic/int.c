#include <common.h>
#include <arch_intf.h>


void *g_handlers[256];

PCPU_DATA int   g_int_depth = 0;
PCPU_DATA void *g_int_stack = NULL;


INIT_TEXT void int_init() {
    for (int i = 0; i < cpu_count(); ++i) {
        int *pdepth = pcpu_ptr(i, &g_int_depth);
        *pdepth = 0;
    }
    // g_int_depth
}
