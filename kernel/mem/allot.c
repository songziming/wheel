#include <wheel.h>

static __INITDATA          u8 temp_buff[CFG_TEMP_ALLOT_SIZE];
static __SECTION(".allot") u8 perm_buff[CFG_PERM_ALLOT_SIZE];

static __INITDATA u8 * temp_ptr = &temp_buff[0];
static __INITDATA u8 * perm_ptr = &perm_buff[0];

__INIT void * allot_temporary(usize len) {
    void * obj = (void *) temp_ptr;
    temp_ptr  += ROUND_UP(len, sizeof(usize));
    if (temp_ptr > &temp_buff[CFG_TEMP_ALLOT_SIZE]) {
        panic("CFG_TEMP_ALLOT_SIZE not sufficient!\n");
    }
    return obj;
}

__INIT void * allot_permanent(usize len) {
    void * obj = (void *) perm_ptr;
    perm_ptr  += ROUND_UP(len, sizeof(usize));
    if (perm_ptr > &perm_buff[CFG_PERM_ALLOT_SIZE]) {
        panic("CFG_PERM_ALLOT_SIZE not sufficient!\n");
    }
    return obj;
}
