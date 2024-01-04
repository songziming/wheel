#include <wheel.h>
#include <page.h>
#include <task.h>
#include <str.h>

#include <arch_mem.h>
#include <arch_smp.h>
#include <arch_cpu.h>
#include <arch_int.h>

#include <dev/acpi.h>
#include <dev/acpi_madt.h>
#include <dev/serial.h>
#include <dev/console.h>
#include <dev/framebuf.h>

#include <cpu/rw.h>
#include <cpu/local_apic.h>

#include <init/multiboot1.h>
#include <init/multiboot2.h>



static INIT_DATA size_t g_rsdp = INVALID_ADDR;
static INIT_DATA fb_info_t g_fb = { .rows=0, .cols=0 };
static INIT_DATA volatile int g_cpu_started = 0; // 已完成启动的 CPU 数量
static task_t root_tcb;


static INIT_TEXT void mb1_init(uint32_t ebx);
static INIT_TEXT void mb2_init(uint32_t ebx);
static INIT_TEXT void sys_init_ap();
static void root_proc();


// 图形终端输出回调
static void serial_framebuf_puts(const char *s, size_t n) {
    serial_puts(s, n);
    framebuf_puts(s, n);
}

// 字符终端输出回调
static void serial_console_puts(const char *s, size_t n) {
    serial_puts(s, n);
    console_puts(s, n);
}



// 8259 PIC port
#define PIC1            0x20        // IO base address for PIC master
#define PIC2            0xA0        // IO base address for PIC slave
#define PIC1_CMD        PIC1
#define PIC1_DAT        (PIC1 + 1)
#define PIC2_CMD        PIC2
#define PIC2_DAT        (PIC2 + 1)

// 8259 PIC control words
#define ICW1_ICW4       0x01        // ICW4 (not) needed
#define ICW1_SINGLE     0x02        // Single (cascade) mode
#define ICW1_INTERVAL4  0x04        // Call address interval 4 (8)
#define ICW1_LEVEL      0x08        // Level triggered (edge) mode
#define ICW1_INIT       0x10        // Initialization - required!
#define ICW4_8086       0x01        // 8086/88 (MCS-80/85) mode
#define ICW4_AUTO       0x02        // Auto (normal) EOI
#define ICW4_BUF_SLAVE  0x08        // Buffered mode/slave
#define ICW4_BUF_MASTER 0x0C        // Buffered mode/master
#define ICW4_SFNM       0x10        // Special fully nested (not)

static void disable_i8259() {
    out8(PIC1_CMD, ICW1_INIT + ICW1_ICW4);
    out8(PIC2_CMD, ICW1_INIT + ICW1_ICW4);
    out8(PIC1_DAT, 0x20);       // ICW2: map master PIC vector base
    out8(PIC2_DAT, 0x28);       // ICW2: map slave PIC vector base
    out8(PIC1_DAT, 4);          // ICW3: slave PIC at IRQ2 (0000 0100)
    out8(PIC2_DAT, 2);          // ICW3: slave PIC's identity (0000 0010)
    out8(PIC1_DAT, ICW4_8086);
    out8(PIC2_DAT, ICW4_8086);
    out8(PIC1_DAT, 0xff);       // mask all pins on master chip
    out8(PIC2_DAT, 0xff);       // mask all pins on slave chip
}



//------------------------------------------------------------------------------
// BSP 初始化流程，使用初始栈，从 GRUB 跳转而来
//------------------------------------------------------------------------------

INIT_TEXT void sys_init(uint32_t eax, uint32_t ebx) {
    // if (g_cpu_started > 0) {
    //     sys_init_ap(g_cpu_started);
    // }
    if (AP_BOOT_MAGIC == eax) {
        sys_init_ap();
    }

    // 临时串口输出
    serial_init();
    set_log_func(serial_puts);

    // 解析 multiboot 信息
    switch (eax) {
    case MB1_BOOTLOADER_MAGIC: mb1_init(ebx); break;
    case MB2_BOOTLOADER_MAGIC: mb2_init(ebx); break;
    default:
        klog("fatal: unknown multibooot magic %x\n", eax);
        goto end;
    }

    // 寻找 RSDP，并找出所有 ACPI 表
    if (INVALID_ADDR == g_rsdp) {
        g_rsdp = acpi_find_rsdp();
    }
    if (INVALID_ADDR == g_rsdp) {
        klog("fatal: RSDP not found!\n");
        goto end;
    }
    acpi_parse_rsdp(g_rsdp);

    // 解析 MADT，获取多核信息
    madt_t *madt = (madt_t *)acpi_get_table("APIC");
    if (NULL == madt) {
        klog("fatal: MADT not found!\n");
        goto end;
    }
    parse_madt(madt);

    // SRAT 可以获取 NUMA 信息
    acpi_tbl_t *srat = acpi_get_table("SRAT");
    if (NULL != srat) {
        klog("SRAT at %p\n", srat);
    }

    // 重要数据已备份，放开 early_rw 长度限制
    early_rw_unlock();

    // 检查是否支持图形界面，使用不同输出方式
    if ((0 != g_fb.rows) && (0 != g_fb.cols)) {
        framebuf_init(&g_fb);
        set_log_func(serial_framebuf_puts);
    } else {
        console_init();
        set_log_func(serial_console_puts);
    }

    cpu_info_detect(); // 检测 CPU 特性
    cpu_features_init(); // 开启 CPU 功能

#ifdef DEBUG
    acpi_show_tables();
    pmmap_show();
    cpu_info_show();
#endif

    // 切换正式 gdt，加载 idt
    gdt_init();
    gdt_load();
    idt_init();
    idt_load();

    // 划分内存布局，启用物理页面管理
    mem_init();
    gsbase_init(0);

    // 加载 tss（依赖 pcpu，需要等 gsbase 之后）
    tss_init_load();

    // 启用中断异常机制
    int_init();

    // 关闭 8259 PIC
    disable_i8259();

    // 设置中断控制器，包含时钟
    local_apic_init(1);

    // 创建并加载内核页表，启用内存保护
    kernel_proc_init();
    write_cr3(get_kernel_pgtable());

    // 首次中断保存上下文
    task_t dummy;
    *(task_t **)this_ptr(&g_tid_prev) = &dummy;

    // 启动第一个任务
    task_create(&root_tcb, "root", root_proc, NULL);
    *(task_t **)this_ptr(&g_tid_next) = &root_tcb;
    arch_task_yield();

end:
    // emu_exit(0);
    cpu_halt();
    while (1) {}
}

static INIT_TEXT void mb1_init(uint32_t ebx) {
    mb1_info_t *info = (mb1_info_t *)(size_t)ebx;

    if (MB1_INFO_MEM_MAP & info->flags) {
        pmmap_init_mb1(info->mmap_addr, info->mmap_length);
    }

    if (MB1_INFO_ELF_SHDR & info->flags) {
        mb1_elf_sec_tbl_t *elf = &info->elf;
        symtab_init((void *)(size_t)elf->addr, elf->size, elf->num);
    }

    if (MB1_INFO_FRAMEBUFFER_INFO & info->flags) {
        if (1 == info->fb_type) {
            g_fb.addr  = info->fb_addr;
            g_fb.rows  = info->fb_height;
            g_fb.cols  = info->fb_width;
            g_fb.bpp   = info->fb_bpp;
            g_fb.pitch = info->fb_pitch;

            g_fb.r.position  = info->r_field_position;
            g_fb.r.mask_size = info->r_mask_size;
            g_fb.g.position  = info->g_field_position;
            g_fb.g.mask_size = info->g_mask_size;
            g_fb.b.position  = info->b_field_position;
            g_fb.b.mask_size = info->b_mask_size;
        }
    }
}

static INIT_TEXT void mb2_init(uint32_t ebx) {
    size_t info = (size_t)ebx;
    uint32_t total_size = *(uint32_t *)info;

    uint32_t offset = 8;
    while (offset < total_size) {
        mb2_tag_t *tag = (mb2_tag_t *)(info + offset);
        offset += (tag->size + 7) & ~7;

        switch (tag->type) {
        case MB2_TAG_TYPE_MMAP:
            pmmap_init_mb2(tag);
            break;
        case MB2_TAG_TYPE_ELF_SECTIONS: {
            mb2_tag_elf_sections_t *elf = (mb2_tag_elf_sections_t *)tag;
            symtab_init(elf->sections, elf->entsize, elf->num);
            break;
        }
        case MB2_TAG_TYPE_ACPI_OLD:
            g_rsdp = (size_t)((mb2_tag_old_acpi_t *)tag)->rsdp;
            break;
        case MB2_TAG_TYPE_ACPI_NEW:
            g_rsdp = (size_t)((mb2_tag_new_acpi_t *)tag)->rsdp;
            break;
        case MB2_TAG_TYPE_FRAMEBUFFER: {
            mb2_tag_framebuffer_t *fb = (mb2_tag_framebuffer_t *)tag;
            if (1 == fb->type) {
                g_fb.addr  = fb->addr;
                g_fb.rows  = fb->height;
                g_fb.cols  = fb->width;
                g_fb.bpp   = fb->bpp;
                g_fb.pitch = fb->pitch;

                g_fb.r.position  = fb->r_field_position;
                g_fb.r.mask_size = fb->r_mask_size;
                g_fb.g.position  = fb->g_field_position;
                g_fb.g.mask_size = fb->g_mask_size;
                g_fb.b.position  = fb->b_field_position;
                g_fb.b.mask_size = fb->b_mask_size;
            }
            break;
        }
        default:
            break;
        }
    }
}



//------------------------------------------------------------------------------
// 根任务，运行在 BSP，使用任务栈，由 bsp 启动
//------------------------------------------------------------------------------

// layout.ld
char _real_addr;
char _real_end;

// 第一个开始运行的任务
static void root_proc() {
    klog("running in root task\n");

    // 将实模式启动代码复制到 1M 以下
    char *from = &_real_addr;
    char *to = (char *)KERNEL_REAL_ADDR + DIRECT_MAP_ADDR;
    kmemcpy(to, from, &_real_end - from);

    // 启动代码地址页号就是 startup-IPI 的向量号
    int vec = KERNEL_REAL_ADDR >> 12;

    g_cpu_started = 1;
    for (int i = 1; i < cpu_count(); ++i) {
        klog("starting cpu %d...", i);

        local_apic_emit_init(i);        // 发送 INIT
        local_apic_busywait(10000);     // 等待 10ms
        local_apic_emit_sipi(i, vec);   // 发送 startup-IPI
        local_apic_busywait(200);       // 等待 200us
        local_apic_emit_sipi(i, vec);   // 再次发送 startup-IPI
        local_apic_busywait(200);       // 等待 200us

        // 每个 AP 使用相同的栈，必须等前一个 AP 启动完成再启动下一个
        while (g_cpu_started == i) {
            cpu_pause();
        }
        klog("done\n");
    }

    cpu_halt();
    while (1) {}
}



//------------------------------------------------------------------------------
// AP 初始化流程，使用初始栈，由根任务启动
//------------------------------------------------------------------------------

static INIT_TEXT void sys_init_ap() {
    // klog("running in processor #%d!\n", cpu);

    cpu_features_init();
    gdt_load();
    idt_load();

    gsbase_init(g_cpu_started);
    ASSERT(g_cpu_started == cpu_index());
    tss_init_load();

    local_apic_init(0);

    write_cr3(get_kernel_pgtable());

    task_t dummy;
    *(task_t **)this_ptr(&g_tid_prev) = &dummy;

    // TODO 让 sched 决定执行哪个任务（应该是 idle-task）

    ++g_cpu_started;

    cpu_halt();
    while (1) {}
}
