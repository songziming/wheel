#include <wheel.h>

static __INITDATA          u8 temp_buff[CFG_TEMP_ALLOT_SIZE];
static __SECTION(".allot") u8 perm_buff[CFG_PERM_ALLOT_SIZE];

static __INITDATA usize temp_used = 0;
static __INITDATA usize perm_used = 0;

__INIT void * allot_temporary(usize len) {
    void * obj = (void *) &temp_buff[temp_used];
    temp_used += ROUND_UP(len, sizeof(usize));
    if (temp_used > CFG_TEMP_ALLOT_SIZE) {
        panic("CFG_TEMP_ALLOT_SIZE not sufficient!\n");
    }
    return obj;
}

__INIT void * allot_permanent(usize len) {
    void * obj = (void *) &perm_buff[perm_used];
    perm_used += ROUND_UP(len, sizeof(usize));
    if (perm_used > CFG_PERM_ALLOT_SIZE) {
        panic("CFG_PERM_ALLOT_SIZE not sufficient!\n");
    }
    return obj;
}
