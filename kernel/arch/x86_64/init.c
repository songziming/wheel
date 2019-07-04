#include <wheel.h>

//------------------------------------------------------------------------------
// pre-kernel initialization

__INIT __NORETURN void sys_init_bsp(u32 ebx) {
    console_dev_init();

    mb_info_t mbi = * (mb_info_t *) phys_to_virt(ebx);
    char * s      = (char *) phys_to_virt(mbi.boot_loader_name);
    console_write(s, strlen(s));

    while (1) {}
}

__INIT __NORETURN void sys_init_ap() {
    while (1) {}
}
