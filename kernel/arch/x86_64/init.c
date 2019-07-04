#include <wheel.h>

//------------------------------------------------------------------------------
// pre-kernel initialization

__INIT __NORETURN void sys_init_bsp(u32 ebx) {
    // init serial and console device
    serial_dev_init();
    console_dev_init();

    // enable debug output
    dbg_trace_hook = dbg_trace_here;
    dbg_write_hook = dbg_write_text;
    dbg_print("wheel operating system starting up.\n");

    // backup multiboot info
    mb_info_t * mbi = (mb_info_t *) allot_temporary(sizeof(mb_info_t));
    memcpy(mbi, phys_to_virt(ebx), sizeof(mb_info_t));

    char * s = (char *) phys_to_virt(mbi->boot_loader_name);
    dbg_print("loaded by: %s.\n", s);

    while (1) {}
}

__INIT __NORETURN void sys_init_ap() {
    while (1) {}
}
