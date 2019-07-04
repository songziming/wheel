#include <wheel.h>

//------------------------------------------------------------------------------
// pre-kernel initialization

__INIT __NORETURN void sys_init_bsp(u32 ebx) {
    serial_dev_init();
    console_dev_init();

    mb_info_t * mbi = (mb_info_t *) allot_temporary(sizeof(mb_info_t));
    memcpy(mbi, phys_to_virt(ebx), sizeof(mb_info_t));

    char * s = (char *) phys_to_virt(mbi->boot_loader_name);
    console_write(s, strlen(s));

    while (1) {}
}

__INIT __NORETURN void sys_init_ap() {
    while (1) {}
}
