#include <wheel.h>
#include <library/debug.h>
#include <library/symbols.h>
#include <memory/pmlayout.h>
#include <memory/early_alloc.h>
#include <memory/vmspace.h>
#include <proc/sched.h>
#include <proc/tick.h>
#include <proc/work.h>
#include <drivers/block.h>
#include <drivers/pci.h>

#include "multiboot1.h"
#include "multiboot2.h"

#include <devices/serial.h>
#include <devices/console.h>
#include <devices/framebuf.h>
#include <devices/acpi_madt.h>
#include <devices/ata.h>

#include <generic/rw.h>
#include <generic/cpufeatures.h>
#include <generic/gdt_idt_tss.h>

#include <memory/mem_init.h>
#include <memory/percpu.h>
#include <memory/mmu.h>

#include <apic/apic_init.h>
#include <apic/i8259.h>
#include <apic/ioapic.h>
#include <apic/loapic.h>
#include <arch_int.h>



// layout.ld
char _real_addr;
char _real_end;

static INIT_DATA size_t g_rsdp = 0;
static INIT_DATA int g_is_graphical = 0;
static INIT_DATA volatile int g_cpu_started = 0; // 已启动的 CPU 数量

static task_t root_tcb;

static void root_proc();
static INIT_TEXT NORETURN void ap_init(int index);


//------------------------------------------------------------------------------
// 解析物理内存布局
//------------------------------------------------------------------------------

static INIT_TEXT void mb1_parse_mmap(uint32_t mmap, uint32_t len) {
    int range_num = 0;
    for (uint32_t off = 0; off < len;) {
        mb1_mmap_entry_t *ent = (mb1_mmap_entry_t *)(size_t)(mmap + off);
        off += ent->size + sizeof(ent->size);
        ++range_num;
    }

    pmranges_alloc(range_num);

    for (uint32_t off = 0; off < len;) {
        mb1_mmap_entry_t *ent = (mb1_mmap_entry_t *)(size_t)(mmap +off);
        off += ent->size + sizeof(ent->size);

        pmtype_t type = (MB1_MEMORY_AVAILABLE == ent->type)
            ? PM_AVAILABLE : PM_RESERVED;

        pmrange_add(ent->addr, ent->addr + ent->len, type);
    }
}

static INIT_TEXT void mb2_parse_mmap(void *tag) {
    mb2_tag_mmap_t *mmap = (mb2_tag_mmap_t *)tag;
    uint32_t mmap_len = mmap->tag.size - sizeof(mb2_tag_mmap_t);
    int range_num = (int)(mmap_len / mmap->entry_size);

    pmranges_alloc(range_num);

    for (int i = 0; i < range_num; ++i) {
        mb2_mmap_entry_t *ent = &mmap->entries[i];

        pmtype_t type;
        switch (ent->type) {
        case MB2_MEMORY_AVAILABLE:        type = PM_AVAILABLE;   break;
        case MB2_MEMORY_ACPI_RECLAIMABLE: type = PM_RECLAIMABLE; break;
        default:                          type = PM_RESERVED;    break;
        }

        pmrange_add(ent->addr, ent->addr + ent->len, type);
    }
}


//------------------------------------------------------------------------------
// 解析引导器信息
//------------------------------------------------------------------------------

static INIT_TEXT void mb1_init(uint32_t ebx) {
    mb1_info_t *info = (mb1_info_t *)(size_t)ebx;

    if (MB1_INFO_MEM_MAP & info->flags) {
        mb1_parse_mmap(info->mmap_addr, info->mmap_length);
    }

    if (MB1_INFO_ELF_SHDR & info->flags) {
        mb1_elf_sec_tbl_t *elf = &info->elf;
        void *tab = (void *)(size_t)elf->addr;
        parse_kernel_symtab(tab, elf->size, elf->num, elf->shndx);
    }

    if (MB1_INFO_FRAMEBUFFER_INFO & info->flags) {
        if (1 == info->fb_type && 32 == info->fb_bpp) {
            g_is_graphical = 1;
            framebuf_init(info->fb_height, info->fb_width, info->fb_pitch, info->fb_addr);

            uint32_t fg = 0;
            fg |= ((1U << info->r_mask_size) - 1) << info->r_field_position;
            fg |= ((1U << info->g_mask_size) - 1) << info->g_field_position;
            fg |= ((1U << info->b_mask_size) - 1) << info->b_field_position;
            framebuf_set_color(fg);
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
        case MB2_TAG_TYPE_END:
            return;
        // case MB2_TAG_TYPE_CMDLINE:
        // case MB2_TAG_TYPE_BOOT_LOADER_NAME:
        //     continue;
        case MB2_TAG_TYPE_MMAP:
            mb2_parse_mmap(tag);
            break;
        case MB2_TAG_TYPE_ELF_SECTIONS: {
            mb2_tag_elf_sections_t *elf = (mb2_tag_elf_sections_t *)tag;
            parse_kernel_symtab(elf->sections, elf->entsize, elf->num, elf->shndx);
            break;
        }
        case MB2_TAG_TYPE_FRAMEBUFFER: {
            mb2_tag_framebuffer_t *fb = (mb2_tag_framebuffer_t *)tag;
            if (1 == fb->type && 32 == fb->bpp) {
                g_is_graphical = 1;
                framebuf_init(fb->height, fb->width, fb->pitch, fb->addr);

                uint32_t fg = 0;
                fg |= ((1U << fb->r_mask_size) - 1) << fb->r_field_position;
                fg |= ((1U << fb->g_mask_size) - 1) << fb->g_field_position;
                fg |= ((1U << fb->b_mask_size) - 1) << fb->b_field_position;
                framebuf_set_color(fg);
            }
            break;
        }
        case MB2_TAG_TYPE_ACPI_OLD:
            g_rsdp = (size_t)((mb2_tag_old_acpi_t *)tag)->rsdp;
            break;
        case MB2_TAG_TYPE_ACPI_NEW:
            g_rsdp = (size_t)((mb2_tag_new_acpi_t *)tag)->rsdp;
            break;
        default:
            break;
        }
    }
}


//------------------------------------------------------------------------------
// 读写 PCI 地址空间
//------------------------------------------------------------------------------

// 这段代码可以放在 arch_impl.c

#define CONFIG_ADDR 0xcf8
#define CONFIG_DATA 0xcfc

static uint32_t pci_read(uint8_t bus, uint8_t dev, uint8_t func, uint8_t reg) {
    ASSERT(dev < 32);
    ASSERT(func < 8);
    ASSERT(0 == (reg & 3));

    uint32_t addr = ((uint32_t)bus  << 16)
                  | ((uint32_t)dev  << 11)
                  | ((uint32_t)func <<  8)
                  |  (uint32_t)reg
                  | 0x80000000U;
    out32(CONFIG_ADDR, addr);
    return in32(CONFIG_DATA);
}

static void pci_write(uint8_t bus, uint8_t dev, uint8_t func, uint8_t reg, uint32_t data) {
    ASSERT(dev < 32);
    ASSERT(func < 8);
    ASSERT(0 == (reg & 3));

    uint32_t addr = ((uint32_t)bus  << 16)
                  | ((uint32_t)dev  << 11)
                  | ((uint32_t)func <<  8)
                  |  (uint32_t)reg
                  | 0x80000000U;
    out32(CONFIG_ADDR, addr);
    out32(CONFIG_DATA, data);
}

// TODO PCIe 使用 mmio 读写配置空间


//------------------------------------------------------------------------------
// 系统初始化入口点
//------------------------------------------------------------------------------

static void text_log(const char *s, size_t n) {
    serial_puts(s, n);
    console_puts(s, n);
}

static void gui_log(const char *s, size_t n) {
    serial_puts(s, n);
    framebuf_puts(s, n);
}

// devices/hpet.c
void hpet_init();

INIT_TEXT NORETURN void sys_init(uint32_t eax, uint32_t ebx) {
    if (0 == eax && 1 == ebx) {
        ap_init(g_cpu_started);
        goto end;
    }

    serial_init();
    set_log_func(serial_puts);

    cpu_features_detect();
    cpu_features_enable();

    // 解析 multiboot 信息
    switch (eax) {
    case MB1_BOOTLOADER_MAGIC: mb1_init(ebx); break;
    case MB2_BOOTLOADER_MAGIC: mb2_init(ebx); break;
    default:
        log("fatal: unknown multibooot magic 0x%08x\n", eax);
        goto end;
    }

    if (g_is_graphical) {
        set_log_func(gui_log);
    } else {
        console_init();
        set_log_func(text_log);
    }

    log("welcome to wheel os\n");
    log("build time %s %s\n", __DATE__, __TIME__);

    if (0 == g_rsdp) {
        g_rsdp = acpi_probe_rsdp();
    }
    if (0 == g_rsdp) {
        log("fatal: ACPI::RSDP not found\n");
        goto end;
    }
    acpi_parse_rsdp(g_rsdp);

    madt_t *madt = (madt_t *)acpi_table_find("APIC", 0);
    if (NULL == madt) {
        log("fatal: MADT not found!\n");
        goto end;
    }
    parse_madt(madt);

    // TODO 检查 Acpi::DMAR，判断是否需要 interrupt remapping
    // TODO 检查 Acpi::SRAT，获取 numa 信息（个人电脑一般不需要）

    if (need_int_remap()) {
        log("APIC ID not representable using 8-bit LDR, needs remapping\n");
    }

    // 关键数据已经备份，可以放开 early-alloc 长度限制
    early_rw_unlock();

    // TODO 如果支持 PCIe，则应该使用 mmio 读写
    if (acpi_table_find("MCFG", 0)) {
        log("has PCIe support!\n");
    }
    pci_lib_init(pci_read, pci_write);
    pci_probe();

    hpet_init();

    // 切换到正式的 GDT，加载 IDT
    gdt_init();
    gdt_load();
    idt_init();
    idt_load();

    // 内存管理初始化
    mem_init();
    thiscpu_init(0);

    // TSS 依赖 thiscpu
    tss_init_load();

    // 中断异常处理机制初始化
    int_init();
    install_ipi_handlers();

    // 中断控制器初始化
    i8259_disable();
    ioapic_init_all();
    loapic_init();

    // 系统时钟（主频 10Hz）
    calibrate_timer();
    tick_init();
    loapic_timer_set_periodic(10);

    // 使用正式内核页表
    write_cr3(kernel_vmspace()->table);

    // 调度初始化
    sched_init();
    work_init();


    // pmlayout_show(); // 打印物理内存布局
    // acpi_tables_show(); // 打印 acpi 表
    // cpu_features_show(); // 打印 cpuinfo


    // 创建根任务
    task_create(&root_tcb, "root", 1, 10, root_proc, 0,0,0,0);
    task_resume(&root_tcb);
    arch_task_switch();

    log("root task cannot start!\n");
end:
    while (1) {
        cpu_pause();
        cpu_halt();
    }
}


//------------------------------------------------------------------------------
// 第一个任务，运行在 CPU 0
//------------------------------------------------------------------------------

static timer_t wd;

static void wd_func(void *a1 UNUSED, void *a2 UNUSED) {
    static int cnt = 0;
    log("watchdog-%d\n", cnt++);

    timer_start(&wd, 20, (timer_func_t)wd_func, 0, 0);
}

static void root_proc() {
    log("running in root task\n");

    // 将实模式代码复制到 1M 以下
    char *from = &_real_addr;
    char *to = (char *)KERNEL_REAL_ADDR + DIRECT_MAP_ADDR;
    memcpy(to, from, &_real_end - from);

    // 启动代码地址页号就是 startup-IPI 的向量号
    int vec = KERNEL_REAL_ADDR >> 12;

    for (int i = 1; i < cpu_count(); ++i) {
        log("starting cpu %d...", i);
        g_cpu_started = i;

        loapic_send_init(i);            // 发送 INIT
        loapic_timer_busywait(10000);   // 等待 10ms
        loapic_send_sipi(i, vec);       // 发送 startup-IPI
        loapic_timer_busywait(200);     // 等待 200us
        loapic_send_sipi(i, vec);       // 再次发送 startup-IPI
        loapic_timer_busywait(200);     // 等待 200us

        // 当 CPU 开始运行 task，说明初始化已经结束，不再使用 init stack
        // 前一个 CPU 初始化完成才能初始化下一个
        volatile task_t **prev = percpu_ptr(i, &g_tid_prev);
        while (0 == strcmp("dummy", (*prev)->stack.desc)) {
            cpu_pause();
        }
    }

    log("all CPU is started\n");

    // TODO 注册设备驱动
    // TODO 启动文件系统
    block_device_lib_init();
    ata_driver_init();

    // TODO 运行相关测试（检查 boot 参数）
    timer_start(&wd, 20, (timer_func_t)wd_func, 0, 0);

    // loapic_send_ipi(-1, 0x80);

    log("root task exiting...\n");
}


//------------------------------------------------------------------------------
// AP 初始化
//------------------------------------------------------------------------------

static INIT_TEXT NORETURN void ap_init(int index) {
    log("CPU %d started running\n", index);

    cpu_features_enable();
    gdt_load();
    idt_load();

    thiscpu_init(index);
    tss_init_load();

    loapic_init();
    loapic_timer_set_periodic(10);

    write_cr3(kernel_vmspace()->table);

    arch_task_switch();

    log("CPU %d cannot start task!\n");

    while (1) {
        cpu_pause();
        cpu_halt();
    }
}
