#include <init/multiboot1.h>
#include <init/multiboot2.h>

#include <arch_mem.h>
#include <arch_smp.h>
#include <arch_int.h>
#include <arch_api_p.h>

#include <cpu/rw.h>
#include <cpu/info.h>
#include <cpu/gdt_idt_tss.h>
#include <cpu/local_apic.h>
#include <cpu/io_apic.h>

#include <dev/acpi.h>
#include <dev/acpi_madt.h>
#include <dev/serial.h>
#include <dev/console.h>
#include <dev/framebuf.h>
#include <dev/i8259.h>
#include <dev/i8042.h>
#include <dev/ata.h>

#include <wheel.h>
#include <shell.h>




static INIT_DATA size_t g_rsdp = INVALID_ADDR;
// static mb2_tag_vbe_t *g_vbe = NULL;
static INIT_DATA fb_info_t g_fb = { .rows=0, .cols=0 };
static INIT_DATA volatile int g_cpu_started = 0; // 已完成启动的 CPU 数量
static task_t root_tcb;


static INIT_TEXT void mb1_init(uint32_t ebx);
static INIT_TEXT void mb2_init(uint32_t ebx);
static INIT_TEXT NORETURN void sys_init_ap();
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



//------------------------------------------------------------------------------
// BSP 初始化流程，使用初始栈，从 GRUB 跳转而来
//------------------------------------------------------------------------------

// vbe.c
void vbe_parse(mb2_tag_vbe_t *tag);

INIT_TEXT NORETURN void sys_init(uint32_t eax, uint32_t ebx) {
    if (AP_BOOT_MAGIC == eax) {
        sys_init_ap();
    }

    // 临时串口输出
    serial_init();
    set_log_func(serial_puts);

    cpu_info_detect(); // 检测 CPU 特性
    cpu_features_init(); // 开启 CPU 功能

    // 解析 multiboot 信息
    switch (eax) {
    case MB1_BOOTLOADER_MAGIC: mb1_init(ebx); break;
    case MB2_BOOTLOADER_MAGIC: mb2_init(ebx); break;
    default:
        klog("fatal: unknown multibooot magic %x\n", eax);
        goto end;
    }

    // 检查是否支持图形界面，使用不同输出方式
    if ((0 != g_fb.rows) && (0 != g_fb.cols)) {
        framebuf_init(&g_fb);
        set_log_func(serial_framebuf_puts);
    } else {
        console_init();
        set_log_func(serial_console_puts);
    }

    // if (g_vbe) {
    //     vbe_parse(g_vbe);
    // }

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

    // 检查是否有必要启用 interrupt remapping（依赖 VT-d）
    acpi_tbl_t *dmar = acpi_get_table("DMAR");
    if (NULL != dmar) {
        klog("DMAR at %p, supports VT-d interrupt remapping\n", dmar);
    }
    if (requires_int_remap()) {
        klog("requires int remap!\n");
    }

    // SRAT 可以获取 NUMA 信息
    acpi_tbl_t *srat = acpi_get_table("SRAT");
    if (NULL != srat) {
        klog("SRAT at %p, contains NUMA info\n", srat);
        // 记录每个 CPU 属于哪个 NUMA domain
    }

    // 重要数据已备份，放开 early_rw 长度限制
    early_rw_unlock();

    // // 检查是否支持图形界面，使用不同输出方式
    // if ((0 != g_fb.rows) && (0 != g_fb.cols)) {
    //     framebuf_init(&g_fb);
    //     set_log_func(serial_framebuf_puts);
    // } else {
    //     console_init();
    //     set_log_func(serial_console_puts);
    // }

    // 准备 PCI 支持
    arch_pci_lib_init(acpi_get_table("MCFG"));
    pci_probe();

    // 切换正式 gdt，加载 idt
    gdt_init();
    gdt_load();
    idt_init();
    idt_load();

    // 划分内存布局，启用物理页面管理
    // kernel_heap_init();
    kernel_context_init();
    mem_init();
    gsbase_init(0);

    // 加载 tss（依赖 pcpu，需要等 gsbase 之后）
    tss_init_load();

    // 启用中断异常机制
    int_init();

    set_int_handler(14, handle_pagefault); // 注册 page fault 处理函数
    install_resched_handlers(); // 注册 resched 中断处理函数

    i8259_disable(); // 禁用 PIC
    io_apic_init_all();
    local_apic_init(); // 设置中断控制器
    local_apic_timer_set(TIMER_FREQ, LOCAL_APIC_TIMER_PERIODIC);

    // 创建并加载内核页表，启用内存保护
    kernel_pgtable_init();
    kernel_context_map_all();
    write_cr3(get_kernel_pgtable());

    // 准备就绪队列
    sched_lib_init();

    // 准备延迟工作队列（此时中断关闭，不会触发tick）
    timer_lib_init();
    // tick_init();
    // work_init();

    // 首次中断保存上下文
    task_t dummy;
    THISCPU_SET(g_tid_prev, &dummy);

    // 创建第一个任务
    task_create(&root_tcb, "root", 0, root_proc);
    root_tcb.affinity = 0;
    task_resume(&root_tcb);
    arch_task_switch();

end:
    emu_exit(0);
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
        // case MB2_TAG_TYPE_VBE: {
        //     // TODO 还有 multiboot 1 也加入 VBE 支持
        //     mb2_tag_vbe_t *vbe = (mb2_tag_vbe_t *)tag;
        //     g_vbe = vbe;
        //     break;
        // }
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



// TODO 专门创建一个 init/drivers.c，用来注册各种设备的驱动
// TODO 有些 PCI 驱动支持多种 vendor/device 组合

void vmware_svga_init(uint8_t bus, uint8_t slot, uint8_t func); // vmware_svga.c
// void ata_pci_lib_init(uint8_t bus, uint8_t slot, uint8_t func); // ata_pci.c


// 识别 PCI 设备，调用驱动
static INIT_TEXT void install_pci_dev(const pci_dev_t *dev) {
    if ((0x15ad == dev->vendor) && (0x0405 == dev->device)) {
        vmware_svga_init(dev->bus, dev->slot, dev->func);
        return;
    }

    if ((1 == dev->classcode) && (1 == dev->subclass)) {
        ata_pci_lib_init(dev);
        return;
    }
}



// 第一个开始运行的任务
static void root_proc() {
    klog("running in root task\n");

    // 将实模式启动代码复制到 1M 以下
    char *from = &_real_addr;
    char *to = (char *)KERNEL_REAL_ADDR + DIRECT_MAP_ADDR;
    memcpy(to, from, &_real_end - from);

    // 启动代码地址页号就是 startup-IPI 的向量号
    int vec = KERNEL_REAL_ADDR >> 12;

    g_cpu_started = 1;
    for (int i = 1; i < cpu_count(); ++i) {
        klog("starting cpu %d...\n", i);

        local_apic_send_init(i);        // 发送 INIT
        local_apic_busywait(10000);     // 等待 10ms
        local_apic_send_sipi(i, vec);   // 发送 startup-IPI
        local_apic_busywait(200);       // 等待 200us
        local_apic_send_sipi(i, vec);   // 再次发送 startup-IPI
        local_apic_busywait(200);       // 等待 200us

        // 每个 AP 使用相同的栈，必须等前一个 AP 启动完成再启动下一个
        // 当 AP 开始运行 idle task，说明该 AP 已完成初始化，不再使用 init stack
        // 必须使用双指针，因为更新的是 g_tid_prev 的指向，而非指向的内容
        volatile task_t **prev = pcpu_ptr(i, &g_tid_prev);
        while ((NULL == *prev) || (NULL == (*prev)->name)) {
            cpu_pause();
        }
    }

#if 0
    // 压力测试
    test_spin_lock();
#endif

    // 注册各种设备的驱动
    block_device_lib_init();
    partition_driver_init();
    ata_driver_init();

    // 枚举 PCI 总线上的设备
    pci_enumerate(install_pci_dev);


    // test_block_io();

    i8042_init(); // PS/2 键盘控制器

    // 启动系统服务
    keyboard_init();
    shell_init();

    // 初始化已将完成，回收 init section
    reclaim_init();
}



//------------------------------------------------------------------------------
// AP 初始化流程，使用初始栈，由根任务启动
//------------------------------------------------------------------------------

static INIT_TEXT NORETURN void sys_init_ap() {
    cpu_features_init();
    gdt_load();
    idt_load();

    gsbase_init(g_cpu_started);
    ASSERT(g_cpu_started == cpu_index());
    tss_init_load();

    local_apic_init();
    local_apic_timer_set(TIMER_FREQ, LOCAL_APIC_TIMER_PERIODIC);

    write_cr3(get_kernel_pgtable());

    task_t dummy = {
        .name = NULL,
    };
    THISCPU_SET(g_tid_prev, &dummy);

    ++g_cpu_started;
    arch_task_switch();

    emu_exit(0);
    // cpu_halt();
    // while (1) {}
}
