#include <wheel.h>

// local APIC memory mapped register offset
#define LOAPIC_ID           0x0020      // local APIC id reg
#define LOAPIC_VER          0x0030      // local APIC version reg
#define LOAPIC_TPR          0x0080      // task priority reg
#define LOAPIC_APR          0x0090      // arbitration priority reg
#define LOAPIC_PPR          0x00a0      // processor priority reg
#define LOAPIC_EOI          0x00b0      // end of interrupt reg
#define LOAPIC_RRD          0x00c0      // remote read reg
#define LOAPIC_LDR          0x00d0      // logical destination reg
#define LOAPIC_DFR          0x00e0      // destination format reg
#define LOAPIC_SVR          0x00f0      // spurious interrupt reg
#define LOAPIC_ISR          0x0100      // 8 in-service regs, 0x0100~0x0170
#define LOAPIC_TMR          0x0180      // 8 trigger mode regs, 0x0180~0x01f0
#define LOAPIC_IRR          0x0200      // 8 interrupt request regs, 0x0200~0x0270
#define LOAPIC_ESR          0x0280      // error status reg
#define LOAPIC_CMCI         0x02f0      // LVT (CMCI)
#define LOAPIC_ICR_LO       0x0300      // int-command-reg upper half
#define LOAPIC_ICR_HI       0x0310      // int-command-reg lower half
#define LOAPIC_TIMER        0x0320      // LVT (timer)
#define LOAPIC_THERMAL      0x0330      // LVT (thermal)
#define LOAPIC_PMC          0x0340      // LVT (performance counter)
#define LOAPIC_LINT0        0x0350      // LVT (LINT0)
#define LOAPIC_LINT1        0x0360      // LVT (LINT1)
#define LOAPIC_ERROR        0x0370      // LVT (error)
#define LOAPIC_ICR          0x0380      // timer Initial count reg
#define LOAPIC_CCR          0x0390      // timer current count reg
#define LOAPIC_CFG          0x03e0      // timer divide config reg

// IA32_APIC_BASE msr
#define IA32_APIC_BASE      0x1b        // MSR index
#define LOAPIC_MSR_BASE     0xfffff000  // local APIC base addr mask
#define LOAPIC_MSR_ENABLE   0x00000800  // local APIC global enable
#define LOAPIC_MSR_BSP      0x00000100  // local APIC is bsp

// local APIC vector table bits
#define LOAPIC_VECTOR       0x000000ff  // vector number mask
#define LOAPIC_MODE         0x00000700  // delivery mode mask
#define LOAPIC_FIXED        0x00000000  // delivery mode: fixed
#define LOAPIC_LOWEST       0x00000100  // delivery mode: lowest
#define LOAPIC_SMI          0x00000200  // delivery mode: SMI
#define LOAPIC_NMI          0x00000400  // delivery mode: NMI
#define LOAPIC_INIT         0x00000500  // delivery mode: INIT
#define LOAPIC_STARTUP      0x00000600  // delivery mode: startup
#define LOAPIC_EXT          0x00000700  // delivery mode: ExtINT
#define LOAPIC_IDLE         0x00000000  // delivery status: idle
#define LOAPIC_PEND         0x00001000  // delivery status: pend
#define LOAPIC_HIGH         0x00000000  // polarity: High
#define LOAPIC_LOW          0x00002000  // polarity: Low
#define LOAPIC_REMOTE       0x00004000  // remote IRR
#define LOAPIC_DEASSERT     0x00000000  // level: de-assert
#define LOAPIC_ASSERT       0x00004000  // level: assert
#define LOAPIC_EDGE         0x00000000  // trigger mode: Edge
#define LOAPIC_LEVEL        0x00008000  // trigger mode: Level
#define LOAPIC_INT_MASK     0x00010000  // interrupt disabled mask

// local APIC spurious-interrupt reg bits
#define LOAPIC_SVR_ENABLE   0x00000100  // APIC enabled

// local APIC timer reg only bits
#define LOAPIC_ONESHOT      0x00000000  // timer mode: one-shot
#define LOAPIC_PERIODIC     0x00020000  // timer mode: periodic
#define LOAPIC_DEADLINE     0x00040000  // timer mode: tsc-deadline

typedef struct loapic {
    u8 apic_id;
    u8 processor_id;
} loapic_t;
static loapic_t loapic_devs[64];

static u64  loapic_addr   = 0;  // mapped physical address
static u8 * loapic_base   = 0;  // mapped virtual address
static u32  loapic_tmr_hz = 0;  // how many cycles in a second

//------------------------------------------------------------------------------
// local apic interrupt service routines

// extern void tick_advance();

static void loapic_resched_proc(int vec) {
    assert(vec == VECNUM_RESCHED);
    // no need to call any function
    loapic_send_eoi();
}

static void loapic_flushmmu_proc(int vec) {
    assert(vec == VECNUM_FLUSHMMU);
    // TODO: clear all tlb entries
    loapic_send_eoi();
}

static void loapic_svr_proc(int vec) {
    assert(vec == VECNUM_SPURIOUS);
    // no need to send EOI
}

static void loapic_timer_proc(int vec) {
    assert(vec == VECNUM_TIMER);
    // tick_advance();
    loapic_send_eoi();
}

//------------------------------------------------------------------------------
// local apic public interface

u8 loapic_get_id() {
    return (read32(loapic_base + LOAPIC_ID) >> 24) & 0xff;
}

void loapic_send_eoi() {
    write32(loapic_base + LOAPIC_EOI, 0);
}

void loapic_emit_ipi(int cpu, int vec) {
    u32 icr_hi = ((u32) loapic_devs[cpu].apic_id << 24) & 0xff000000;
    u32 icr_lo = (vec & 0xff) | LOAPIC_FIXED | LOAPIC_EDGE | LOAPIC_DEASSERT;
    write32(loapic_base + LOAPIC_ICR_HI, icr_hi);
    write32(loapic_base + LOAPIC_ICR_LO, icr_lo);
}

//------------------------------------------------------------------------------
// local apic initialization routines

// override default local apic mapping address
__INIT void loapic_override(u64 addr) {
    loapic_addr = addr;
    loapic_base = (u8 *) phys_to_virt(addr);
}

// register a local apic instance, cpu_installed already increased
__INIT void loapic_dev_add(madt_loapic_t * tbl) {
    if (cpu_installed < 64) {
        loapic_devs[cpu_installed].apic_id      = tbl->id;
        loapic_devs[cpu_installed].processor_id = tbl->processor_id;
        ++cpu_installed;
    }
}

// TODO: set LINT0 and LINT1
__INIT void loapic_set_nmi(madt_loapic_mni_t * tbl __UNUSED) {
    //
}

// calculate how many cycle per second
static __INIT int calibrate_freq() {
    write32(loapic_base + LOAPIC_CFG, 0x0b);        // divide by 1
    write32(loapic_base + LOAPIC_ICR, 0xffffffff);  // max minus 1

    // use pit channel 2 mode 3, set counter to 65535
    out8(0x43, 0xb6);
    out8(0x42, 0xff);
    out8(0x42, 0xff);

    // set initial gate input as high, save loapic counter
    out8(0x61, in8(0x61) | 0x01);
    u32 start_count = read32(loapic_base + LOAPIC_CCR);

    // wait 50ms
    while (1) {
        out8(0x43, 0xe8);
        if ((in8(0x42) & 0x80) != 0x80) {
            break;
        }
    }
    while (1) {
        out8(0x43, 0x80);
        u8 lo8 = in8(0x42);
        u8 hi8 = in8(0x42);
        if (((hi8 << 8) + lo8) <= (2 * 65535 - 119318 + 10)) {
            break;
        }
    }

    // now read loapic counter again, and disable PIT channel 2
    u32 end_count = read32(loapic_base + LOAPIC_CCR);
    out8(0x61, in8(0x61) & ~0x01);
    return (start_count - end_count) * 20;  // 1s = 50ms
}

// initialize the local apic of current cpu
// this function registers isr, must be called after int_init
__INIT void loapic_dev_init() {
    u64 msr = read_msr(IA32_APIC_BASE);
    if ((loapic_addr & LOAPIC_MSR_BASE) != (msr & LOAPIC_MSR_BASE)) {
        msr = loapic_addr & LOAPIC_MSR_BASE;
    }
    write_msr(IA32_APIC_BASE, msr | LOAPIC_MSR_ENABLE);

    write32(loapic_base + LOAPIC_DFR, 0xffffffff);
    write32(loapic_base + LOAPIC_TPR, 0);
    write32(loapic_base + LOAPIC_ICR, 0);
    write32(loapic_base + LOAPIC_CFG, 0);

    // disable all lvt entries
    write32(loapic_base + LOAPIC_CMCI,    LOAPIC_INT_MASK);
    write32(loapic_base + LOAPIC_TIMER,   LOAPIC_INT_MASK);
    write32(loapic_base + LOAPIC_THERMAL, LOAPIC_INT_MASK);
    write32(loapic_base + LOAPIC_PMC,     LOAPIC_INT_MASK);
    write32(loapic_base + LOAPIC_LINT0,   LOAPIC_INT_MASK);
    write32(loapic_base + LOAPIC_LINT1,   LOAPIC_INT_MASK);
    write32(loapic_base + LOAPIC_ERROR,   LOAPIC_INT_MASK);

    // enable this loapic device
    write32(loapic_base + LOAPIC_SVR, 0x200 | LOAPIC_SVR_ENABLE | VECNUM_SPURIOUS);

    // discard pending interrupts
    write32(loapic_base + LOAPIC_EOI, 0);

    // set default isr functions
    if (0 == loapic_tmr_hz) {
        // isr_tbl[VECNUM_RESCHED ] = loapic_resched_proc;
        // isr_tbl[VECNUM_FLUSHMMU] = loapic_flushmmu_proc;
        // isr_tbl[VECNUM_SPURIOUS] = loapic_svr_proc;
        // isr_tbl[VECNUM_TIMER   ] = loapic_timer_proc;
        loapic_tmr_hz = calibrate_freq();
    }

    // start the timer
    // TODO: set system frequency in conf header
    write32(loapic_base + LOAPIC_TIMER, LOAPIC_PERIODIC | VECNUM_TIMER);
    write32(loapic_base + LOAPIC_CFG, 0x0b);
    write32(loapic_base + LOAPIC_ICR, loapic_tmr_hz / 1000);
}

// send init IPI to the target cpu
__INIT void loapic_emit_init(int cpu) {
    u32 icr_hi = ((u32) loapic_devs[cpu].apic_id << 24) & 0xff000000;
    u32 icr_lo = LOAPIC_INIT | LOAPIC_EDGE | LOAPIC_ASSERT;
    write32(loapic_base + LOAPIC_ICR_HI, icr_hi);
    write32(loapic_base + LOAPIC_ICR_LO, icr_lo);
}

// send startup IPI to the target cpu
__INIT void loapic_emit_sipi(int cpu, int vec) {
    u32 icr_hi = ((u32) loapic_devs[cpu].apic_id << 24) & 0xff000000;
    u32 icr_lo = (vec & 0xff) | LOAPIC_STARTUP | LOAPIC_EDGE | LOAPIC_ASSERT;
    write32(loapic_base + LOAPIC_ICR_HI, icr_hi);
    write32(loapic_base + LOAPIC_ICR_LO, icr_lo);
}
