#include <wheel.h>
#include <arch_api.h>

#include "multiboot1.h"
#include "multiboot2.h"

#include <cpu/features.h>
#include <cpu/gdt_idt_tss.h>
#include <acpi/madt.h>
#include <apic/apic.h>
#include <mem/mem.h>
#include <arch_int.h>

#include <dev/serial.h>
#include <dev/console.h>
#include <dev/framebuf.h>
#include <dev/i8259.h>
#include <dev/hpet.h>

#include <early_alloc.h>
#include <pmlayout.h>
#include <task.h>
#include <work.h>
#include <ktimer.h>
#include <kstring.h>
#include <spin.h>
// #include <semaphore.h>
#include <debug.h>
#include <ktest.h>


// layout.ld
char _real_addr;
char _real_end;

static INIT_BSS uint32_t g_fgcolor;
static INIT_BSS size_t   g_rsdp;

static INIT_BSS task_t g_root_tcb;
static INIT_TEXT void root_proc();


static INIT_DATA int g_cpu_started = 1;
// static INIT_BSS semaphore_t g_smp_sem;
static INIT_BSS work_t g_smp_notifier;
static INIT_TEXT NORETURN void ap_init(int idx);


static INIT_TEXT void mb1_parse_mmap(uint32_t mmap, uint32_t len) {
    g_pmrange_num = 0;
    for (uint32_t off = 0; off < len;) {
        mb1_mmap_entry_t *ent = (mb1_mmap_entry_t*)(size_t)(mmap + off);
        off += ent->size + sizeof(ent->size);
        ++g_pmrange_num;
    }

    g_pmranges = early_alloc_ro(g_pmrange_num * sizeof(pmrange_t));

    for (int i = 0; i < g_pmrange_num; ++i) {
        mb1_mmap_entry_t *ent = (mb1_mmap_entry_t*)(size_t)mmap;
        mmap += ent->size + sizeof(ent->size);

        g_pmranges[i].start = ent->addr;
        g_pmranges[i].end   = ent->addr + ent->len;
        g_pmranges[i].type  = (MB1_MEMORY_AVAILABLE == ent->type) ? PM_AVAILABLE : PM_RESERVED;
    }
}

static INIT_TEXT void mb2_parse_mmap(void *tag) {
    mb2_tag_mmap_t *mmap = (mb2_tag_mmap_t*)tag;
    uint32_t mmap_len = mmap->tag.size - sizeof(mb2_tag_mmap_t);

    g_pmrange_num = (int)(mmap_len / mmap->entry_size);
    g_pmranges = early_alloc_ro(g_pmrange_num * sizeof(pmrange_t));

    for (int i = 0; i < g_pmrange_num; ++i) {
        mb2_mmap_entry_t *ent = &mmap->entries[i];

        g_pmranges[i].start = ent->addr;
        g_pmranges[i].end   = ent->addr + ent->len;
        switch (ent->type) {
        case MB2_MEMORY_AVAILABLE:        g_pmranges[i].type = PM_AVAILABLE;   break;
        case MB2_MEMORY_ACPI_RECLAIMABLE: g_pmranges[i].type = PM_RECLAIMABLE; break;
        default:                          g_pmranges[i].type = PM_RESERVED;    break;
        }
    }
}

static INIT_TEXT void mb1_init(uint32_t ebx) {
    mb1_info_t *info = (mb1_info_t*)(size_t)ebx;

    if (MB1_INFO_MEM_MAP & info->flags) {
        mb1_parse_mmap(info->mmap_addr, info->mmap_length);
    }

    if (MB1_INFO_FRAMEBUFFER_INFO & info->flags) {
        if (1 == info->fb_type && 32 == info->fb_bpp) {
            g_fgcolor  = ((1U << info->r_size) - 1) << info->r_shift;
            g_fgcolor |= ((1U << info->g_size) - 1) << info->g_shift;
            g_fgcolor |= ((1U << info->b_size) - 1) << info->b_shift;
            logk("framebuf mapped at 0x%lx\n", info->fb_addr);
            framebuf_init(info->fb_height, info->fb_width, info->fb_pitch, info->fb_addr);
        }
    }
}

static INIT_TEXT void mb2_init(uint32_t ebx) {
    size_t info = (size_t)ebx;
    uint32_t size = *(uint32_t*)info;

    for (uint32_t off = 8; off < size;) {
        mb2_tag_t *tag = (mb2_tag_t*)(info + off);
        off += (tag->size + 7) & ~7;

        switch (tag->type) {
        case MB2_TAG_TYPE_END:
            return;
        case MB2_TAG_TYPE_MMAP:
            mb2_parse_mmap(tag);
            break;

        case MB2_TAG_TYPE_FRAMEBUFFER: {
            mb2_tag_framebuffer_t *fb = (mb2_tag_framebuffer_t*)tag;
            if (1 == fb->type && 32 == fb->bpp) {
                g_fgcolor  = ((1U << fb->r_size) - 1) << fb->r_shift;
                g_fgcolor |= ((1U << fb->g_size) - 1) << fb->g_shift;
                g_fgcolor |= ((1U << fb->b_size) - 1) << fb->b_shift;
                logk("framebuf mapped at 0x%lx\n", fb->addr);
                framebuf_init(fb->height, fb->width, fb->pitch, fb->addr);
            }
            break;
        }
        case MB2_TAG_TYPE_ACPI_OLD:
            g_rsdp = (size_t)((mb2_tag_old_acpi_t*)tag)->rsdp;
            break;
        case MB2_TAG_TYPE_ACPI_NEW:
            g_rsdp = (size_t)((mb2_tag_new_acpi_t*)tag)->rsdp;
            break;
        default:
            break;
        }
    }
}

static INIT_TEXT void text_log(const char *s, size_t n) {
    serial_puts(s, n);
    console_puts(s, n);
}

static INIT_TEXT void gui_log(const char *s, size_t n) {
    serial_puts(s, n);
    framebuf_puts(s, n);
}

INIT_TEXT NORETURN void sys_init(uint32_t eax, uint32_t ebx) {
    if (0 == eax) {
        ap_init(g_cpu_started++);
    }

    serial_init();
    g_log_func = serial_puts;

    cpu_features_detect();
    cpu_features_enable();

    // parse multiboot info
    g_fgcolor = 0;
    g_rsdp = 0;
    switch (eax) {
    case MB1_BOOTLOADER_MAGIC: mb1_init(ebx); break;
    case MB2_BOOTLOADER_MAGIC: mb2_init(ebx); break;
    }

    if (g_fgcolor) {
        g_log_func = gui_log;
    } else {
        console_init();
        g_log_func = text_log;
    }
    logk("Wheel Operating System (%s %s)\n", __DATE__, __TIME__);

    // parse ACPI tables
    if (0 == g_rsdp) {
        g_rsdp = acpi_rsdp_probe();
    }
    if (0 == g_rsdp) {
        logk("fatal: cannot find ACPI RSDP\n");
        goto end;
    }
    acpi_rsdp_parse(g_rsdp);

    // parse APIC info
    madt_t *madt = (madt_t*)acpi_table_find("APIC", 0);
    if (NULL == madt) {
        logk("fatal: cannot find MADT!\n");
        goto end;
    }
    parse_madt(madt);

    // 内存中的关键数据已备份，可以放开 early-rw 增长限制
    early_rw_unlock();

    // 创建正式的 gdt、idt
    gdt_init();
    gdt_load();
    idt_init();
    idt_load();

    // 初始化内存管理
    mem_init(); // this also init percpu
    thiscpu_init(0);
    ASSERT(cpu_index() == 0);

    // 死锁检查，依赖 pthiscpu
    lockdep_enable();

    thistss_init_load(0); // 依赖 thiscpu，需要放在 thiscpu_init 之后
    int_init(); // 初始化中断管理机制
    int_init_local();

    // TODO 将8254替换成精度更高的时钟，用来校准 local apic timer
    // hpet_init();

    // 中断控制器初始化
    i8259_disable();
    ioapic_init();
    loapic_init();
    loapic_init_local();

    // 校准时钟
    loapic_timer_calibrate();
    loapic_timer_set_periodic(50);

    // 加载正式页表，此后 CONST 变为只读
    write_cr3(g_kernel_vm.table);

    // 初始化任务调度
    work_init_this();
    timer_init();
    sched_init();

    // 创建根任务并开始运行，优先级 30，仅高于 idle
    task_create(&g_root_tcb, "root", 30, root_proc);
    g_root_tcb.affinity = 0;
    task_start(&g_root_tcb);
    // THISCPU_SET(g_tid_next, &g_root_tcb);
    arch_task_switch();
    // 之后的代码不再运行

end:
    while (1) {
        cpu_pause();
        cpu_halt();
    }
}

// 第一个运行的任务，运行在 BSP
static INIT_TEXT void root_proc() {
    cpu_features_show();

    // for (int i = 0; i < 1000; ++i) {
    //     logk("R%d", i);
    //     loapic_timer_busywait(20000);
    // }

    for (int i = 0; i < 10; ++i) {
        logk("testing round #%d:\n", i);
        test_cooperative();
    }

    while (1) {
        cpu_pause();
        cpu_halt();
    }

    // 将实模式代码复制到 1M 以下
    char *from = &_real_addr;
    char *to = (char*)KERNEL_REAL_ADDR + DIRECT_MAP_ADDR;
    kmemcpy(to, from, &_real_end - from);
    logk("copy trampoline code from %p to %p\n", from, to);

    // semaphore_init(&g_smp_sem, 0, 1);

    // 启动代码地址页号就是 startup-IPI 的向量号
    int vec = KERNEL_REAL_ADDR >> 12;
    for (int i = 1; i < cpu_count(); ++i) {
        logk("(BSP) starting cpu %d...", i);

        loapic_send_init(i);            // 发送 INIT
        loapic_timer_busywait(10000);   // 等待 10ms
        loapic_send_sipi(i, vec);       // 发送 startup-IPI
        loapic_timer_busywait(200);     // 等待 200us
        loapic_send_sipi(i, vec);       // 再次发送 startup-IPI
        loapic_timer_busywait(200);     // 等待 200us

        // 当 CPU 开始运行 task，说明初始化已经结束，不再使用 init stack
        // 前一个 CPU 初始化完成才能初始化下一个
        // semaphore_take(&g_smp_sem, 1, FOREVER);
    }

    // logk("all CPU running\n");
    // arch_send_ipi(-1, VEC_IPI_RESCHED);

    logk("stop system\n");
    arch_send_ipi(-1, VEC_IPI_STOPALL);

    while (1) {
        cpu_pause();
        cpu_halt();
    }
}

// 通知 BSP，又一个 AP 初始化完成，开始运行 task，不再使用 init-stack
// BSP 可以启动下一个 AP，或者将 init-stack 回收
static INIT_TEXT void notify_ap_started(work_t *work UNUSED) {
    ASSERT(cpu_int_depth() > 0);
    // semaphore_give(&g_smp_sem, 1);
}

// AP 启动流程，使用 init-stack
// 多个 CPU 不能同时执行此函数，因为共用同一个栈
static INIT_TEXT NORETURN void ap_init(int idx) {
    logk("CPU-%d started\n", idx);

    // size_t sp;
    // ASMV("movq %%rsp, %0" : "=r"(sp));
    // logk("AP-%d current stack pointer 0x%zx\n", idx, sp);

    cpu_features_enable();
    gdt_load();
    idt_load();

    thiscpu_init(idx);
    ASSERT(cpu_index() == idx);

    thistss_init_load(idx);
    int_init_local();

    loapic_init_local();
    loapic_timer_set_periodic(2);
    write_cr3(g_kernel_vm.table);   // 加载正式页表

    work_init_this();
    sched_init();

    // 注册一个 work，在切换任务时执行（使用中断栈）
    // 只有不再使用这个 init-stack，才能安全地启动另一个 CPU
    work_defer(&g_smp_notifier, notify_ap_started, "notify ap start");
    arch_task_switch();

    while (1) {
        cpu_pause();
        cpu_halt();
    }
}
