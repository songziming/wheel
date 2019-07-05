#include <wheel.h>

//------------------------------------------------------------------------------
// kernel debug support

// static void trace_symbol(u64 addr) {
//     elf64_sym_t * func = NULL;
//     usize         dist = (usize) -1;

//     if ((NULL != symtbl_addr) && (NULL != strtbl_addr)) {
//         for (usize i = 0; i < symtbl_size; ++i) {
//             if ((STT_FUNC == (symtbl_addr[i].st_info & 0x0f)) &&
//                 (symtbl_addr[i].st_value < addr)              &&
//                 ((addr - symtbl_addr[i].st_value) < dist)) {
//                 dist = addr - symtbl_addr[i].st_value;
//                 func = &symtbl_addr[i];
//             }
//         }
//     }

//     if (NULL == func) {
//         dbg_print("--> 0x%016llx.\n", addr);
//     } else {
//         dbg_print("--> 0x%016llx (%s + 0x%x).\n",
//             addr, &strtbl_addr[func->st_name], addr - func->st_value);
//     }
// }

void dbg_trace_here() {
    u64 * rbp;
    ASM("movq %%rbp, %0" : "=r"(rbp));

    // until we got a NULL return address
    while (rbp[1]) {
        // trace_symbol(rbp[1]);
        rbp = (u64 *) rbp[0];
    }
}

void dbg_trace_from(u64 rip, u64 * rbp) {
    // trace_symbol(rip);

    // until we got a NULL return address
    while (rbp[1]) {
        // trace_symbol(rbp[1]);
        rbp = (u64 *) rbp[0];
    }
}

void dbg_write_text(const char * s, usize len) {
    serial_write (s, len);
    console_write(s, len);
}

__INIT void regist_symtbl(void * tbl, usize len) {
    // symtbl_addr = (elf64_sym_t *) tbl;
    // symtbl_size = len / sizeof(elf64_sym_t);
}

__INIT void regist_strtbl(void * tbl, usize len) {
    // strtbl_addr = (char *) tbl;
    // strtbl_size = len;
}

//------------------------------------------------------------------------------
// multi-processor support

           int    cpu_installed = 0;
__INITDATA int    cpu_activated = 0;
           u64    percpu_base   = 0;    // cpu0's offset to its percpu area
           u64    percpu_size   = 0;    // length of one per-cpu area

int cpu_count() {
    return cpu_installed;
}

int cpu_index() {
    return (int) ((read_gsbase() - percpu_base) / percpu_size);
}

void * calc_percpu_addr(u32 cpu, void * ptr) {
    return (void *) ((char *) ptr + percpu_base + percpu_size * cpu);
}

void * calc_thiscpu_addr(void * ptr) {
    return (void *) ((char *) ptr + read_gsbase());
}
