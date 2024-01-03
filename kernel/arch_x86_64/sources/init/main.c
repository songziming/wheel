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

#include <cpu/local_apic.h>

#include <init/multiboot1.h>
#include <init/multiboot2.h>



static INIT_DATA size_t g_rsdp = INVALID_ADDR;
static INIT_DATA fb_info_t g_fb = { .rows=0, .cols=0 };



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





static void root_proc();
static void app_proc();

static task_t root_tcb;
static task_t app_tcb;


// BSP 初始化函数
INIT_TEXT void sys_init(uint32_t eax, uint32_t ebx) {
    if (AP_BOOT_MAGIC == eax) {
        klog("running in another processor!\n");
        cpu_halt();
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
    klog("SRAT at %p\n", srat);

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

    // 设置中断控制器，包含时钟
    local_apic_init_bsp();

    // 创建并加载内核页表，启用内存保护
    kernel_proc_init();

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



void dummy_wait() {
    for (int i = 0; i < 10000; ++i) {
        for (int j = 0; j < 10000; ++j) {
            __asm__ volatile("nop");
        }
    }
}


// layout.ld
char _real_addr;
char _real_end;

// 第一个开始运行的任务
static void root_proc() {
    klog("greeting from root proc\n");

    // 将实模式启动代码复制到 1M 以下
    char *from = &_real_addr;
    char *to = (char *)KERNEL_REAL_ADDR + KERNEL_TEXT_ADDR;
    kmemcpy(to, from, &_real_end - from);

    // 启动代码地址
    int vec = KERNEL_REAL_ADDR >> 12;

    for (int i = 1; i < cpu_count(); ++i) {
        local_apic_emit_init(i);        // 发送 INIT
        local_apic_busywait(10000);     // 等待 10ms
        local_apic_emit_sipi(i, vec);   // 发送 SIPI
        local_apic_busywait(200);       // 等待 200us
        local_apic_emit_sipi(i, vec);   // 再次发送 SIPI again
        local_apic_busywait(200);       // 等待 200us

        // 每个 AP 使用相同的栈，必须等前一个 AP 启动完成再启动下一个
        break;

        // // same boot stack is used, have to start cpus one-by-one
        // while ((percpu_var(i, tid_prev) == NULL) ||
        //        (percpu_var(i, tid_prev)->priority > PRIORITY_IDLE)) {
        //     tick_delay(10);
        // }
    }

    // int iter = 0;
    // task_create(&app_tcb, "app", app_proc, NULL);

    // while (1) {
    //     klog(" R%d", ++iter);
    //     dummy_wait();
    //     *(task_t **)this_ptr(&g_tid_next) = &app_tcb;
    //     // arch_task_yield();
    // }

    cpu_halt();
    while (1) {}
}


static void app_proc() {
    int iter = 0;
    while (1) {
        klog(" A%d", ++iter);
        dummy_wait();
        dummy_wait();
        *(task_t **)this_ptr(&g_tid_next) = &root_tcb;
        // arch_task_yield();
    }

    cpu_halt();
    while (1) {}
}
