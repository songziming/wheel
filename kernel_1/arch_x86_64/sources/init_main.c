// 初始化流程

#include <base.h>
#include <debug.h>
#include <tick.h>
#include <page.h>
#include <libk.h>

#include <multiboot1.h>
#include <multiboot2.h>
#include <arch_debug.h>
#include <arch_mem.h>
#include <arch_smp.h>
#include <arch_interface.h>
#include <arch_int.h>

#include <liba/rw.h>
#include <liba/cpuid.h>
#include <liba/cpu.h>

#include <dev/acpi_base.h>
#include <dev/i8259.h>
#include <dev/loapic.h>
#include <dev/serial.h>
#include <dev/console.h>
#include <dev/framebuf.h>





static INIT_DATA acpi_rsdp_t *g_rsdp = NULL;
static INIT_DATA mb2_tag_framebuffer_t *g_tagfb = NULL;

static INIT_DATA int g_cpu_started = 1;

static task_t g_root_task;
static PCPU_BSS task_t g_idle_task;

static void root_proc();
static void idle_proc();


//------------------------------------------------------------------------------
// task 启动之前 BSP 的初始化流程
//------------------------------------------------------------------------------

// 解析 Multiboot 1.0 引导信息
static INIT_TEXT void mb1_init(uint32_t ebx) {
    mb1_info_t *info = (mb1_info_t *)(size_t)ebx;

    if (MB1_INFO_MEM_MAP & info->flags) {
        // 首先统计可用内存范围的数量，提前申请好空间，然后再填充数据
        int valid_num = 0;
        for (uint32_t off = 0; off < info->mmap_length;) {
            mb1_mmap_entry_t *ent = (mb1_mmap_entry_t *)(size_t)(info->mmap_addr + off);
            off += ent->size + sizeof(ent->size);
            if (MB1_MEMORY_AVAILABLE == ent->type) {
                ++valid_num;
            }
        }
        ram_range_reserve(valid_num);
        int valid_idx = 0;
        for (uint32_t off = 0; off < info->mmap_length;) {
            mb1_mmap_entry_t *ent = (mb1_mmap_entry_t *)(size_t)(info->mmap_addr + off);
            off += ent->size + sizeof(ent->size);
            if (MB1_MEMORY_AVAILABLE == ent->type) {
                ram_range_set(valid_idx++, ent->addr, ent->len);
            }
        }
    }

    if (MB1_INFO_ELF_SHDR & info->flags) {
        mb1_elf_sec_tbl_t *elf = &info->elf;
        ASSERT(sizeof(Elf64_Shdr) == elf->size);
        dbg_sym_init((Elf64_Shdr *)(size_t)elf->addr, elf->num);
    }
}

// 解析 Multiboot 2.0 引导信息
static INIT_TEXT void mb2_init(uint32_t ebx) {
    size_t info = (size_t)ebx;
    uint32_t total_size = *(uint32_t *)info;

    uint32_t offset = 8;
    while (offset < total_size) {
        mb2_tag_t *tag = (mb2_tag_t *)(info + offset);
        offset += (tag->size + 7) & ~7; // tag 必须 8 字节对齐

        switch (tag->type) {
        case MB2_TAG_TYPE_MMAP: {
            // 首先统计可用内存范围的数量，提前申请好空间，然后再填充数据
            mb2_tag_mmap_t *mmap = (mb2_tag_mmap_t *)tag;
            size_t end = (size_t)tag + mmap->tag.size;
            int available_num = 0;
            int reclaimable_num = 0;
            for (size_t pos = (size_t)mmap->entries; pos < end; pos += mmap->entry_size) {
                mb2_mmap_entry_t *ent = (mb2_mmap_entry_t *)pos;
                if (MB2_MEMORY_AVAILABLE == ent->type) {
                    ++available_num;
                } else if (MB2_MEMORY_ACPI_RECLAIMABLE == ent->type) {
                    ++reclaimable_num;
                }
            }
            ram_range_reserve(available_num + reclaimable_num);
            int range_idx = 0;
            for (size_t pos = (size_t)mmap->entries; pos < end; pos += mmap->entry_size) {
                mb2_mmap_entry_t *ent = (mb2_mmap_entry_t *)pos;
                if ((MB2_MEMORY_AVAILABLE == ent->type) || (MB2_MEMORY_ACPI_RECLAIMABLE == ent->type)) {
                    ram_range_set(range_idx++, ent->addr, ent->len);
                }
            }
            ASSERT(available_num + reclaimable_num == range_idx);
            break;
        }
        case MB2_TAG_TYPE_ELF_SECTIONS: {
            mb2_tag_elf_sections_t *elf = (mb2_tag_elf_sections_t *)tag;
            ASSERT(sizeof(Elf64_Shdr) == elf->entsize);
            dbg_sym_init((Elf64_Shdr *)elf->sections, elf->num);
            break;
        }
        case MB2_TAG_TYPE_ACPI_OLD:
            g_rsdp = (acpi_rsdp_t *)((mb2_tag_old_acpi_t *)tag)->rsdp;
            break;
        case MB2_TAG_TYPE_ACPI_NEW:
            g_rsdp = (acpi_rsdp_t *)((mb2_tag_new_acpi_t *)tag)->rsdp;
            break;
        case MB2_TAG_TYPE_FRAMEBUFFER:
            g_tagfb = (mb2_tag_framebuffer_t *)tag;
            break;
        // case MB2_TAG_TYPE_VBE:
        //     g_vbe_ctrl = (vbe_ctrl_t *)((mb2_tag_vbe_t *)tag)->vbe_control_info;
        //     g_vbe_mode = (vbe_mode_t *)((mb2_tag_vbe_t *)tag)->vbe_mode_info;
        //     break;
        // case MB2_TAG_TYPE_EFI64:
        //     g_uefi_sys_table = ((mb2_tag_efi64_t *)tag)->pointer;
        //     break;
        // case MB2_TAG_TYPE_EFI64_IH:
        //     g_uefi_img_handle = ((mb2_tag_efi64_image_handle_t *)tag)->pointer;
        //     break;
        default:
            break;
        }
    }
}


// tick.c
void task_init(task_t *task, void *entry, size_t stack_size, vmspace_t *vm, size_t tbl);

INIT_TEXT NORETURN void sys_init(uint32_t eax, uint32_t ebx) {
    serial_init();
    g_print_func = serial_puts;

    early_alloc_init();

    // 解析 Multiboot 信息，两个版本都支持
    // 这一步包括探测物理内存、解析内核符号
    switch (eax) {
    case MB1_BOOTLOADER_MAGIC:
        mb1_init(ebx);
        break;
    case MB2_BOOTLOADER_MAGIC:
        mb2_init(ebx);
        break;
    default:
        dbg_print("error: unknown multibooot magic %x\n", eax);
        cpu_halt();
    }

    if (NULL != g_tagfb) {
        framebuf_init(g_tagfb);
        g_print_func = serial_framebuf_puts;
    } else {
        console_init();
        g_print_func = serial_console_puts;
    }

    dbg_print("CPU 0 started\n");

    // 寻找并解析 ACPI 表
    if (NULL == g_rsdp) {
        g_rsdp = acpi_probe_rsdp();
    }
    if (NULL == g_rsdp) {
        dbg_print("error: RSDP not found!\n");
        cpu_halt();
    }
    acpi_parse_rsdp(g_rsdp);

    // 解析 MADT，获取 APIC 信息，同时记录 CPU 数量
    madt_t *madt = (madt_t *)acpi_get_table("APIC");
    if (NULL == madt) {
        dbg_print("error: MADT not found!\n");
        cpu_halt();
    }
    parse_madt(madt);

    // 内存里重要数据已经备份（符号表、mmap、acpi、madt 等）
    // 可以放开 early-alloc 的长度限制
    kernel_end_unlock();

    get_cpu_info();     // 通过 cpuid 获取硬件信息，创建页表时需要
    cpu_feat_init();    // 设置 CPU 功能开关，切换新页表时需要

    mem_init();         // 划分内存布局，启动 page-alloc，建立内核页表（但不切换）
    gsbase_init(0);     // 使用 GS.base 访问 thiscpu-var

    gdt_init_load();    // 切换正式 GDT，依赖 per cpu
    tss_init_load();    // 设置任务状态段，依赖 per cpu
    idt_init();         // 填充 IDT，依赖 page-alloc
    idt_load();         // 加载 IDT
    int_init();         // 准备中断表和中断栈（依赖 mmu、vmspace 和 per cpu）
    i8259_disable();    // 禁用所有的 8259 中断
    loapic_init();

    write_cr3(g_kernel_map); // 切换新页表

    // // 内核虚拟地址范围大部分是固定的，只有任务栈、内存池等结构需要动态申请
    // // 限制这些动态内存的地址范围，低地址留给用户进程
    // g_kernel_vm.start = 0xffffa00000000000;
    // g_kernel_vm.end   = 0xffffc00000000000;

    // 准备 idle task 的资源
    task_t *idle = this_ptr(&g_idle_task);
    task_init(idle, idle_proc, IDLE_STACK_SIZE, &g_kernel_vm, g_kernel_map);


    // 创建根任务
    task_init(&g_root_task, root_proc, ROOT_STACK_SIZE, &g_kernel_vm, g_kernel_map);

    // 开始运行根任务，切换之后自动打开中断
    task_regs_t curr;
    __asm__("movq %0, %%gs:(g_tid_curr)" :: "r"(&curr));
    __asm__("movq %0, %%gs:(g_tid_next)" :: "r"(&g_root_task));
    switch_task();

    dbg_print("error: cannot launch root task\n");
    cpu_halt();
    while (1) {}
}


//------------------------------------------------------------------------------
// task 启动之前 AP 的初始化流程
//------------------------------------------------------------------------------

void show_cpu_info();

INIT_TEXT NORETURN void sys_init_ap() {
    dbg_print("CPU %d started\n", g_cpu_started);

    gsbase_init(g_cpu_started); // 启用 thiscpu
    cpu_feat_init();            // 启用写保护、NX、1G-page
    write_cr3(g_kernel_map);    // 加载正式页表
    gdt_init_load();            // 设置并加载正式 GDT
    tss_init_load();            // 设置并加载正式 TSS
    idt_load();                 // 加载正式 IDT
    loapic_init();

    // 准备 idle task 的资源
    task_t *idle = this_ptr(&g_idle_task);
    task_init(idle, idle_proc, IDLE_STACK_SIZE, &g_kernel_vm, g_kernel_map);

    // 开始运行 idle task
    task_regs_t curr;
    __asm__("movq %0, %%gs:(g_tid_curr)" :: "r"(&curr));
    __asm__("movq %0, %%gs:(g_tid_next)" :: "r"(idle));
    switch_task();

    dbg_print("error: cannot launch idle task\n");
    cpu_halt();
    while (1) {}
}


//------------------------------------------------------------------------------
// task 启动之后的初始化流程
//------------------------------------------------------------------------------

void show_cpu_info();

// 定义在 layout.ld
extern uint8_t _real_addr;
extern uint8_t _real_end;

// 运行在 BSP，第一个任务
static void root_proc() {
    dbg_print("CPU 0 running root task\n");

    // 将 AP 启动代码复制到 1M 之前
    uint8_t *real_code = (void *)(DIRECT_MAP_BASE + AP_REALMODE_ADDR);
    size_t real_size = (size_t)(&_real_end - &_real_addr);
    memcpy(real_code, &_real_addr, real_size);

#if 1
    // 启动所有处理器
    // STARTUP IPI 向量号左移 12 比特就是启动代码地址
    int vec = AP_REALMODE_ADDR >> 12;
    g_cpu_started = 1;
    for (int i = 1; i < g_loapic_count; ++i) {
        uint32_t target = g_loapics[i].apic_id;

        loapic_emit_init(target);
        loapic_busywait(10000); // 10ms
        loapic_emit_startup(target, vec);
        loapic_busywait(200); // 200us
        loapic_emit_startup(target, vec);
        loapic_busywait(200); // 200us

        // 目标 CPU 开始运行 task，说明已经完成启动，不再使用 init_stack
        task_t **next = pcpu_ptr(i, &g_tid_next);
        while (NULL == *next) {
            cpu_pause();
        }
        ++g_cpu_started;
    }
    dbg_print("all processor init done\n");
#endif

    // 显示内核虚拟地址空间布局
    vmspace_show(&g_kernel_vm);
    mmu_show(g_kernel_map);

    // // 测试 framebuf 滚屏性能
    // for (int i = 0; i < 20; ++i) {
    //     dbg_print("lorem ipsum %d\n", i);
    // }

#if 0
    // 测试精确计时
    while (1) {
        dbg_print("tsc waiting\n");
        // tsc_busywait(1000000);
        loapic_busywait(1000000);
    }
#endif

    // TODO 各组件已经启动，可以在这里运行单元测试
    // 退出 QEMU，指定返回值
    __asm__("outl %0, %1" :: "a"(1), "Nd"(0xf4));

    while (1) {}
}

// idle task 栈空间很小，不能做太多事情
static void idle_proc() {
    while (1) {
        cpu_halt();
    }
}
