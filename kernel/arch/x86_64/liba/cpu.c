#include <wheel.h>

//------------------------------------------------------------------------------
// kernel debug support

static char        * strtbl_addr = NULL;
static usize         strtbl_size = 0;
static elf64_sym_t * symtbl_addr = NULL;
static usize         symtbl_size = 0;

static void resolve_and_print(u64 addr) {
    elf64_sym_t * func = NULL;
    usize         dist = (usize) -1;

    if ((NULL != symtbl_addr) && (NULL != strtbl_addr)) {
        for (usize i = 0; i < symtbl_size; ++i) {
            if ((STT_FUNC == (symtbl_addr[i].st_info & 0x0f)) &&
                (symtbl_addr[i].st_value < addr)              &&
                ((addr - symtbl_addr[i].st_value) < dist)) {
                dist = addr - symtbl_addr[i].st_value;
                func = &symtbl_addr[i];
            }
        }
    }

    if (NULL == func) {
        dbg_print("--> 0x%016llx.\n", addr);
    } else {
        dbg_print("--> 0x%016llx (%s + 0x%x).\n",
            addr, &strtbl_addr[func->st_name], addr - func->st_value);
    }
}

void dbg_trace_here() {
    u64 * rbp;
    ASM("movq %%rbp, %0" : "=r"(rbp));

    // until we got a NULL return address
    while (rbp[1]) {
        resolve_and_print(rbp[1]);
        rbp = (u64 *) rbp[0];
    }
}

void dbg_trace_from(u64 rip, u64 * rbp) {
    resolve_and_print(rip);

    // until we got a NULL return address
    while (rbp[1]) {
        resolve_and_print(rbp[1]);
        rbp = (u64 *) rbp[0];
    }
}

void dbg_write_text(const char * s, usize len) {
    serial_write (s, len);
    console_write(s, len);
}

__INIT void regist_symtbl(void * tbl, usize len) {
    symtbl_addr = (elf64_sym_t *) tbl;
    symtbl_size = len / sizeof(elf64_sym_t);
}

__INIT void regist_strtbl(void * tbl, usize len) {
    strtbl_addr = (char *) tbl;
    strtbl_size = len;
}

//------------------------------------------------------------------------------
// multi-processor support

           int cpu_installed = 0;
__INITDATA int cpu_activated = 0;
           u64 percpu_base   = 0;   // cpu0's offset to its percpu area
           u64 percpu_size   = 0;   // length of one per-cpu area

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

//------------------------------------------------------------------------------
// essential cpu features

typedef struct idt_ent {
    u16 offset_low;
    u16 selector;
    u16 attr;
    u16 offset_mid;
    u32 offset_high;
    u32 reserved;
} __PACKED idt_ent_t;

typedef struct tss {
    u32 reserved1;
    u32 rsp0_lower;
    u32 rsp0_upper;
    u32 rsp1_lower;
    u32 rsp1_upper;
    u32 rsp2_lower;
    u32 rsp2_upper;
    u32 reserved2;
    u32 reserved3;
    u32 ist1_lower;
    u32 ist1_upper;
    u32 ist2_lower;
    u32 ist2_upper;
    u32 ist3_lower;
    u32 ist3_upper;
    u32 ist4_lower;
    u32 ist4_upper;
    u32 ist5_lower;
    u32 ist5_upper;
    u32 ist6_lower;
    u32 ist6_upper;
    u32 ist7_lower;
    u32 ist7_upper;
    u32 reserved4;
    u32 reserved5;
    u16 reserved6;
    u16 io_map_base;
} __PACKED tss_t;

typedef struct tbl_ptr {
    u16 limit;
    u64 base;
} __PACKED tbl_ptr_t;

// gdt, idt and tss
static   u64     * gdt;
static   idt_ent_t idt[256];
__PERCPU tss_t     tss;

// defined in cpu.S
extern void load_gdtr(tbl_ptr_t * ptr);
extern void load_idtr(tbl_ptr_t * ptr);
extern void load_tr  (u16 sel);

extern void irq_entries  ();
extern void syscall_entry();

__INIT void cpu_init() {
    if (0 == cpu_activated) {
        // TODO: query cpu capabilities (cache line size, floating point support, etc)
    }

    u64 cr0 = read_cr0();
    cr0 |=  (1UL <<  1);        // cr0.MP
    cr0 &= ~(1UL <<  2);        // cr0.EM: disable emulated mode
    cr0 |=  (1UL <<  5);        // cr0.NE: enable native exception
    cr0 |=  (1UL << 16);        // cr0.WP: enable write protection
    write_cr0(cr0);

    // enable NX bit in page entries
    u64 efer = read_msr(0xc0000080);
    efer |= (1UL <<  0);        // enable syscall/sysret on intel processors
    efer |= (1UL << 11);        // no-execute mode enable
    write_msr(0xc0000080, efer);

    // setup msr for syscall/sysret
    write_msr(0xc0000081, 0x001b000800000000UL);    // STAR
    write_msr(0xc0000082, (u64) syscall_entry);     // LSTAR
    write_msr(0xc0000084, 0UL);                     // SFMASK
}

__INIT void gdt_init() {
    if (0 == cpu_activated) {
        gdt = (u64 *) allot_permanent((6 + cpu_count() * 2) * sizeof(u64));
        gdt[0] = 0UL;                   // dummy
        gdt[1] = 0x00a0980000000000UL;  // kernel code
        gdt[2] = 0x00c0920000000000UL;  // kernel data
        gdt[3] = 0UL;                   // reserved
        gdt[4] = 0x00c0f20000000000UL;  // user data
        gdt[5] = 0x00a0f80000000000UL;  // user code
    }
    tbl_ptr_t gdtr;
    gdtr.base  = (u64) gdt;
    gdtr.limit = (6 + cpu_count() * 2) * sizeof(u64) - 1;
    load_gdtr(&gdtr);
}

__INIT void idt_init() {
    if (0 == cpu_activated) {
        u64 entry = (u64) irq_entries;
        for (int i = 0; i < 256; ++i, entry += 16) {
            idt[i].attr        = 0x8e00;  // dpl = 0
            idt[i].selector    = 0x08;
            idt[i].offset_low  =  entry        & 0xffff;
            idt[i].offset_mid  = (entry >> 16) & 0xffff;
            idt[i].offset_high = (entry >> 32) & 0xffffffff;
            idt[i].reserved    = 0;
        }
    }
    tbl_ptr_t idtr;
    idtr.base  = (u64) &idt;
    idtr.limit = 256 * sizeof(idt_ent_t) - 1;
    load_idtr(&idtr);
}

__INIT void tss_init() {
    u32 cpu_idx  = cpu_activated;
    u64 tss_size = (u64) sizeof(tss_t);
    u64 tss_addr = (u64) percpu_ptr(cpu_idx, tss);
    memset((void *) tss_addr, 0, tss_size);

    u64 lower = 0UL;
    u64 upper = 0UL;
    lower |=  tss_size        & 0x000000000000ffffUL;   // limit [15:0]
    lower |= (tss_addr << 16) & 0x000000ffffff0000UL;   // base  [23:0]
    lower |= (tss_size << 32) & 0x000f000000000000UL;   // limit [19:16]
    lower |= (tss_addr << 32) & 0xff00000000000000UL;   // base  [31:24]
    lower |=                    0x0000e90000000000UL;   // present 64bit ring3
    upper  = (tss_addr >> 32) & 0x00000000ffffffffUL;   // base  [63:32]

    gdt[2 * cpu_idx + 6] = lower;
    gdt[2 * cpu_idx + 7] = upper;

    load_tr(((2 * cpu_idx + 6) << 3) | 3);
}

//------------------------------------------------------------------------------
// exception and interrupt

__PERCPU int    int_depth;
__PERCPU usize  int_rsp;
__PERCPU u8     int_stack[CFG_INT_STACK_SIZE];
         void * isr_tbl  [256];

void int_disable() {
    ASM("cli");
}

void int_enable () {
    ASM("sti");
}

u32 int_lock() {
    u64 key;
    ASM("pushfq; cli; popq %0" : "=r"(key));
    return (u32) key & 0x200;
}

void int_unlock(u32 key) {
    if (key & 0x200) {
        ASM("sti");
    }
}

static void exp_default(int vec, int_frame_t * f) {
    static const char * mnemonics[] = {
        "DE", "DB", "NMI","BP", "OF", "BR", "UD", "NM",
        "DF", "??", "TS", "NP", "SS", "GP", "PF", "??",
        "MF", "AC", "MC", "XF", "??", "??", "??", "??",
        "??", "??", "??", "??", "??", "??", "SX", "??"
    };

    dbg_print("\n");
    dbg_print("==> Exception #%s vector=0x%02x.\n", mnemonics[vec], vec);
    dbg_print("    SS:RSP = 0x%02x:0x%016llx\n", f->ss, f->rsp);
    dbg_print("    CS:RIP = 0x%02x:0x%016llx\n", f->cs, f->rip);

    if (0 == (f->cs & 3)) {
        dbg_trace_from(f->rip, (u64 *) f->rbp);
    }

    while (1) {}
}

static void int_default(int vec, int_frame_t * f) {
    dbg_print("\n");
    dbg_print("==> Interrupt vector=0x%02x.\n", vec);
    dbg_print("    SS:RSP = 0x%02x:0x%016llx\n", f->ss, f->rsp);
    dbg_print("    CS:RIP = 0x%02x:0x%016llx\n", f->cs, f->rip);
    while (1) {}
}

// setup interrupt stack, init isr table
__INIT void int_init() {
    for (int i = 0; i < cpu_count(); ++i) {
        percpu_var(i, int_depth) = 0;
        percpu_var(i, int_rsp)   = (u64) percpu_ptr(i, int_stack[CFG_INT_STACK_SIZE]);
    }
    for (int i = 0; i < 32; ++i) {
        isr_tbl[i] = exp_default;
    }
    for (int i = 32; i < 256; ++i) {
        isr_tbl[i] = int_default;
    }
}

//------------------------------------------------------------------------------
// task support

// real entry of task, defined in cpu.S
extern void task_entry();

void regs_init(regs_t * regs, usize sp, void * proc,
               void * a1, void * a2, void * a3, void * a4) {
    assert(0 != regs);
    assert(0 != sp);
    assert(0 != proc);

    // stack pointer must be 8-byte aligned
    sp &= ~7UL;

    regs->rsp  = (int_frame_t *) ((u64) sp - sizeof(int_frame_t));
    regs->rsp0 = (u64) sp;
    regs->cr3  = 0UL;

    memset(regs->rsp, 0, sizeof(int_frame_t));
    regs->rsp->cs     = 0x08UL;             // kernel code segment
    regs->rsp->ss     = 0x10UL;             // kernel data segment
    regs->rsp->rflags = 0x0200UL;           // interrupt enabled
    regs->rsp->rip    = (u64) task_entry;   // entry address
    regs->rsp->rsp    = (u64) sp;           // stack top
    regs->rsp->rax    = (u64) proc;         // pass entry function through rax
    regs->rsp->rdi    = (u64) a1;
    regs->rsp->rsi    = (u64) a2;
    regs->rsp->rdx    = (u64) a3;
    regs->rsp->rcx    = (u64) a4;
}
