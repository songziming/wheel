#include <wheel.h>
#include <arch_api.h>

#include "multiboot1.h"
#include "multiboot2.h"

#include <dev/serial.h>
#include <dev/console.h>
#include <dev/framebuf.h>
#include <cpu/features.h>
#include <cpu/gdt_idt_tss.h>
#include <acpi/madt.h>
#include <apic/apic.h>
#include <mem/mem.h>
#include <arch_int.h>

#include <debug.h>
#include <kstring.h>
#include <pmlayout.h>
#include <early_alloc.h>


static INIT_DATA uint32_t g_fgcolor;
static INIT_DATA size_t   g_rsdp;


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

void sys_init(uint32_t eax, uint32_t ebx) {
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

    // 初始化内存管理
    mem_init(); // this also init percpu
    thiscpu_init(0);

    // 初始化中断管理（tss 依赖 thiscpu，需要放在 thiscpu_init 之后）
    tss_init_load();
    loapic_init();
    int_init();
    idt_load();

    // 加载正式页表
    write_cr3(g_kernel_vm.table);

    pmlayout_show();
    vmspace_show(&g_kernel_vm);
    cpu_features_show();
    // loapic_show();

    ASMV("int $0x80");
    ASMV("ud2");

end:
    while (1) {
        cpu_pause();
        cpu_halt();
    }
}
