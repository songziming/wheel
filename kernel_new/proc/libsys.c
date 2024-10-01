#include <common.h>
#include <arch_intf.h>

cpuset_t cpu_mask() {
    return (1UL << cpu_count()) - 1;
}
