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
#include <dev/vgatext.h>
#include <dev/framebuf.h>
#include <dev/i8259_pit.h>
#include <dev/i8042_kbd.h>
#include <dev/ata_pio.h>

#include <early_alloc.h>
#include <pmlayout.h>

#include <task.h>
#include <work.h>
#include <wdog.h>
#include <sema.h>

#include <kstring.h>
#include <debug.h>

#include <keyboard.h>
#include <kshell.h>
#include <console.h>
#include <block.h>
#include <pci.h>


// layout.ld
char _real_addr;
char _real_end;

static INIT_BSS size_t   g_rsdp;
static INIT_BSS uint32_t g_fbcolor;
static INIT_BSS uint64_t g_fbaddr;
static INIT_BSS uint32_t g_fbheight;
static INIT_BSS uint32_t g_fbwidth;
static INIT_BSS uint32_t g_fbpitch;

// 根任务不能放在 init，需要在这里回收 init-section
static task_t g_root_tcb;
static void root_proc();

static INIT_DATA int g_cpu_started = 1;
static INIT_BSS sema_t g_smp_sema;
static INIT_BSS work_t g_smp_notifier;
static INIT_TEXT NORETURN void ap_init(int idx);


//------------------------------------------------------------------------------
// 解析 grub 传来的信息
//------------------------------------------------------------------------------

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
            g_fbcolor  = ((1U << info->r_size) - 1) << info->r_shift;
            g_fbcolor |= ((1U << info->g_size) - 1) << info->g_shift;
            g_fbcolor |= ((1U << info->b_size) - 1) << info->b_shift;
            g_fbaddr   = info->fb_addr;
            g_fbheight = info->fb_height;
            g_fbwidth  = info->fb_width;
            g_fbpitch  = info->fb_pitch;
            logk("framebuf mapped at 0x%lx\n", info->fb_addr);
            // framebuf_init(info->fb_height, info->fb_width, info->fb_pitch, info->fb_addr);
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
                g_fbcolor  = ((1U << fb->r_size) - 1) << fb->r_shift;
                g_fbcolor |= ((1U << fb->g_size) - 1) << fb->g_shift;
                g_fbcolor |= ((1U << fb->b_size) - 1) << fb->b_shift;
                g_fbaddr   = fb->addr;
                g_fbheight = fb->height;
                g_fbwidth  = fb->width;
                g_fbpitch  = fb->pitch;
                logk("framebuf mapped at 0x%lx\n", fb->addr);
                // framebuf_init(fb->height, fb->width, fb->pitch, fb->addr);
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

//------------------------------------------------------------------------------
// 第一个 CPU 启动过程
//------------------------------------------------------------------------------

// 此时单核、关中断，使用临时栈和临时页表
INIT_TEXT NORETURN void sys_init(uint32_t eax, uint32_t ebx) {
    if (0 == eax) {
        ap_init(g_cpu_started++);
    }

    // 初始化串口，打印日志就用它
    serial_init();
    log_init();
    g_log_func = serial_puts;

    cpu_features_detect();
    cpu_features_enable();

    // parse multiboot info
    g_rsdp = 0;
    g_fbcolor = 0;
    switch (eax) {
    case MB1_BOOTLOADER_MAGIC: mb1_init(ebx); break;
    case MB2_BOOTLOADER_MAGIC: mb2_init(ebx); break;
    }

    // parse ACPI tables
    if (0 == g_rsdp) {
        g_rsdp = acpi_rsdp_probe();
    }
    if (0 == g_rsdp) {
        logk("fatal: cannot find ACPI RSDP\n");
        goto end;
    }
    acpi_rsdp_parse(g_rsdp);

    // PCI (requires ACPI::MCFG)
    arch_pci_init();
    pci_probe();

    // parse APIC info
    madt_t *madt = (madt_t*)acpi_table_find("APIC", 0);
    if (NULL == madt) {
        logk("fatal: cannot find MADT!\n");
        goto end;
    }
    parse_madt(madt);

    // 选择输出设备，用于 console
    if (g_fbcolor) {
        // framebuf 需要用到 PCI（仅虚拟机），但此时 PCI 尚未初始化
        framebuf_init(g_fbheight, g_fbwidth, g_fbpitch, g_fbaddr);
        framebuf_setfg(g_fbcolor);
    } else {
        vgatext_init();
    }
    console_init();
    console_printf("Wheel Operating System (%s %s)\n", __DATE__, __TIME__);

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

    // 开启死锁检查（依赖 thiscpu 和 tid_prev）
    // tid_prev 已经指向 dummy_tcb，且 TCB 里面的 lockdep 也已配置好
    lockdep_enable();

    thistss_init_load(0); // 依赖 thiscpu，需要放在 thiscpu_init 之后
    int_init(); // 初始化中断管理机制
    int_init_local();

    // 中断控制器初始化
    i8259_disable();
    loapic_init();
    loapic_init_local();
    ioapic_init();

    // 校准时钟
    loapic_timer_calibrate();
    loapic_timer_set_periodic(SYSTIMER_FREQ);

    // 加载正式页表，此后 CONST 变为只读
    write_cr3(g_kernel_vm.table);
    if (g_fbcolor) {
        framebuf_remap_wc(); // 重映射为 Write-Combined，提升写显存速度
    }

    // 初始化任务调度
    work_init_this();
    wdog_init();
    sched_init();

    // 创建根任务并开始运行，优先级 30，仅高于 idle
    task_create(&g_root_tcb, "root", 30, root_proc);
    g_root_tcb.affinity = 0;
    task_start_now(&g_root_tcb);
    // 之后的代码不再运行

end:
    while (1) {
        cpu_pause();
        cpu_halt();
    }
}

//------------------------------------------------------------------------------
// 第一个运行的任务，运行在 BSP
//------------------------------------------------------------------------------

static void root_proc() {
    // 将实模式代码复制到 1M 以下
    char *from = &_real_addr;
    char *to = (char*)KERNEL_REAL_ADDR + DIRECT_MAP_ADDR;
    kmemcpy(to, from, &_real_end - from);
    logk("copy trampoline code from %p to %p\n", from, to);

    sema_init(&g_smp_sema, 0, 1);

    // 启动代码地址页号就是 startup-IPI 的向量号
    int vec = KERNEL_REAL_ADDR >> 12;
    for (int i = 1; i < cpu_count(); ++i) {
        logk("(BSP) starting cpu %d...", i);
        loapic_send_init(i);            // 发送 INIT
        loapic_timer_busywait(10000);   // 等待 10ms
        loapic_send_sipi(i, vec);       // 发送 startup-IPI
        loapic_timer_busywait(200);     // 等待 200us
        loapic_send_sipi(i, vec);       // 再次发送 startup-IPI

        // 当 CPU 开始运行 task，说明初始化已经结束，不再使用 init stack
        // 前一个 CPU 初始化完成才能初始化下一个
        sema_take(&g_smp_sema, FOREVER);
    }

    // 系统中间件初始化（console 已经在启动早期初始化）
    // TODO 这部分代码与硬件平台无关，可以提取出来
    keyboard_init();
    block_dev_init();

    // 设备初始化（这些设备依赖前面的系统中间件）
    i8042_init();
    ata_init();

    // 启动内核服务（这也是 init-text）
    kshell_start();

    // 回收 init section
    reclaim_init();

    // TODO root task 不必退出，可以加载 ELF 进入 ring3，开始执行进程
}

//------------------------------------------------------------------------------
// 后续 CPU 的启动过程
//------------------------------------------------------------------------------

// 通知 BSP，又一个 AP 初始化完成，开始运行 task，不再使用 init-stack
// BSP 可以启动下一个 AP，或者将 init-stack 回收
static INIT_TEXT void notify_ap_started(work_t *work UNUSED) {
    ASSERT(cpu_int_depth() > 0);
    sema_give(&g_smp_sema);
}

// AP 启动流程，使用 init-stack
// 多个 CPU 不能同时执行此函数，因为共用同一个栈
static INIT_TEXT NORETURN void ap_init(int idx) {
    logk("CPU-%d started\n", idx);

    cpu_features_enable();
    gdt_load();
    idt_load();

    thiscpu_init(idx);
    ASSERT(cpu_index() == idx);

    thistss_init_load(idx);
    int_init_local();

    loapic_init_local();
    loapic_timer_set_periodic(SYSTIMER_FREQ);
    write_cr3(g_kernel_vm.table);   // 加载正式页表

    work_init_this();
    sched_init();

    // 注册一个 work，在中断返回时执行（使用中断栈）
    // 只有不再使用这个 init-stack，才能安全地启动另一个 CPU
    work_defer(&g_smp_notifier, notify_ap_started, "notify ap start");

    // arch_task_switch 不会执行 work-flush，需要一个中断
    // 此时中断关闭，发送 self-IPI 是收不到的，但 int 指令可以触发
    ASMV("int %0" :: "i"(VEC_IPI_RESCHED));

    while (1) {
        cpu_pause();
        cpu_halt();
    }
}
