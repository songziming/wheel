#include <wheel.h>
#include <page.h>

#include <arch_api_p.h>
#include <arch_mem.h>
#include <arch_smp.h>
#include <arch_cpu.h>

#include <dev/acpi.h>
#include <dev/serial.h>
#include <dev/console.h>
#include <dev/framebuf.h>

#include <init/multiboot1.h>
#include <init/multiboot2.h>



static INIT_DATA acpi_rsdp_t *g_rsdp = NULL;
static INIT_DATA fb_info_t g_fb = { .rows=0, .cols=0 };



static INIT_TEXT void mb1_init(uint32_t ebx) {
    mb1_info_t *info = (mb1_info_t *)(size_t)ebx;

    if (MB1_INFO_MEM_MAP & info->flags) {
        g_rammap_len = 0;
        for (uint32_t off = 0; off < info->mmap_length;) {
            mb1_mmap_entry_t *ent = (mb1_mmap_entry_t *)(size_t)(info->mmap_addr + off);
            off += ent->size + sizeof(ent->size);
            ++g_rammap_len;
        }

        g_rammap = early_alloc_ro(g_rammap_len * sizeof(ram_range_t));

        for (uint32_t off = 0, i = 0; off < info->mmap_length; ++i) {
            mb1_mmap_entry_t *ent = (mb1_mmap_entry_t *)(size_t)(info->mmap_addr + off);
            off += ent->size + sizeof(ent->size);
            if (MB1_MEMORY_AVAILABLE == ent->type) {
                g_rammap[i].type = RAM_AVAILABLE;
            } else {
                g_rammap[i].type = RAM_RESERVED;
            }
            g_rammap[i].addr = ent->addr;
            g_rammap[i].end  = ent->addr + ent->len;
        }
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
        case MB2_TAG_TYPE_MMAP: {
            mb2_tag_mmap_t *mmap = (mb2_tag_mmap_t *)tag;
            uint32_t mmap_len = mmap->tag.size - sizeof(mb2_tag_mmap_t);
            g_rammap_len = (int)(mmap_len / mmap->entry_size);
            g_rammap = early_alloc_ro(g_rammap_len * sizeof(ram_range_t));
            for (int i = 0; i < g_rammap_len; ++i) {
                mb2_mmap_entry_t *ent = &mmap->entries[i];
                switch (ent->type) {
                case MB2_MEMORY_AVAILABLE:        g_rammap[i].type = RAM_AVAILABLE;   break;
                case MB2_MEMORY_ACPI_RECLAIMABLE: g_rammap[i].type = RAM_RECLAIMABLE; break;
                default:                          g_rammap[i].type = RAM_RESERVED;    break;
                }
                g_rammap[i].addr = ent->addr;
                g_rammap[i].end  = ent->addr + ent->len;
            }
            break;
        }
        case MB2_TAG_TYPE_ELF_SECTIONS: {
            mb2_tag_elf_sections_t *elf = (mb2_tag_elf_sections_t *)tag;
            symtab_init(elf->sections, elf->entsize, elf->num);
            break;
        }
        case MB2_TAG_TYPE_ACPI_OLD:
            g_rsdp = (acpi_rsdp_t *)((mb2_tag_old_acpi_t *)tag)->rsdp;
            break;
        case MB2_TAG_TYPE_ACPI_NEW:
            g_rsdp = (acpi_rsdp_t *)((mb2_tag_new_acpi_t *)tag)->rsdp;
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



// BSP 初始化函数
INIT_TEXT void sys_init(uint32_t eax, uint32_t ebx) {
    // 设置临时内存分配、临时串口输出
    early_alloc_init();
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
    if (NULL == g_rsdp) {
        g_rsdp = acpi_find_rsdp();
    }
    if (NULL == g_rsdp) {
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

    __asm__("ud2");

end:
    // emu_exit(0);
    cpu_halt();
    while (1) {}
}
