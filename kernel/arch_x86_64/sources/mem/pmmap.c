#include <arch_mem.h>
#include <wheel.h>
#include <shell.h>

#include <init/multiboot1.h>
#include <init/multiboot2.h>



CONST pmrange_t *g_pmmap = NULL;
CONST int g_pmmap_len = 0;

static shell_cmd_t g_cmd_pmmap;



static INIT_TEXT const char *pmtype_str(pmtype_t type) {
    switch (type) {
    case PM_RESERVED: return "reserved";
    case PM_AVAILABLE: return "available";
    case PM_RECLAIMABLE: return "reclaimable";
    default: return "error";
    }
}

void pmmap_show() {
    ASSERT(NULL != g_pmmap);
    ASSERT(g_pmmap_len > 0);

    klog("ram ranges:\n");
    for (int i = 0; i < g_pmmap_len; ++i) {
        size_t addr = g_pmmap[i].addr;
        size_t end  = g_pmmap[i].end;
        const char *type = pmtype_str(g_pmmap[i].type);
        klog("  - ram range: addr=0x%016zx, end=0x%016zx, type=%s\n", addr, end, type);
    }
}

INIT_TEXT void pmmap_register_cmd() {
    g_cmd_pmmap.name = "pmmap";
    g_cmd_pmmap.func = (void *)pmmap_show;
    shell_add_cmd(&g_cmd_pmmap);
}

INIT_TEXT void pmmap_init_mb1(uint32_t mmap, uint32_t len) {
    ASSERT(NULL == g_pmmap);
    ASSERT(0 == g_pmmap_len);

    g_pmmap_len = 0;
    for (uint32_t off = 0; off < len;) {
        mb1_mmap_entry_t *ent = (mb1_mmap_entry_t *)(size_t)(mmap + off);
        off += ent->size + sizeof(ent->size);
        ++g_pmmap_len;
    }

    g_pmmap = early_alloc_ro(g_pmmap_len * sizeof(pmrange_t));

    for (uint32_t off = 0, i = 0; off < len; ++i) {
        mb1_mmap_entry_t *ent = (mb1_mmap_entry_t *)(size_t)(mmap + off);
        off += ent->size + sizeof(ent->size);
        if (MB1_MEMORY_AVAILABLE == ent->type) {
            g_pmmap[i].type = PM_AVAILABLE;
        } else {
            g_pmmap[i].type = PM_RESERVED;
        }
        g_pmmap[i].addr = ent->addr;
        g_pmmap[i].end  = ent->addr + ent->len;
    }

    pmmap_register_cmd();
}

INIT_TEXT void pmmap_init_mb2(void *tag) {
    ASSERT(NULL == g_pmmap);
    ASSERT(0 == g_pmmap_len);

    mb2_tag_mmap_t *mmap = (mb2_tag_mmap_t *)tag;
    ASSERT(MB2_TAG_TYPE_MMAP == mmap->tag.type);

    uint32_t mmap_len = mmap->tag.size - sizeof(mb2_tag_mmap_t);
    g_pmmap_len = (int)(mmap_len / mmap->entry_size);
    g_pmmap = early_alloc_ro(g_pmmap_len * sizeof(pmrange_t));
    for (int i = 0; i < g_pmmap_len; ++i) {
        mb2_mmap_entry_t *ent = &mmap->entries[i];
        switch (ent->type) {
        case MB2_MEMORY_AVAILABLE:        g_pmmap[i].type = PM_AVAILABLE;   break;
        case MB2_MEMORY_ACPI_RECLAIMABLE: g_pmmap[i].type = PM_RECLAIMABLE; break;
        default:                          g_pmmap[i].type = PM_RESERVED;    break;
        }
        g_pmmap[i].addr = ent->addr;
        g_pmmap[i].end  = ent->addr + ent->len;
    }

    pmmap_register_cmd();
}

pmrange_t *pmmap_locate(size_t ptr) {
    ASSERT(NULL != g_pmmap);
    ASSERT(g_pmmap_len > 0);

    for (int i = 0; i < g_pmmap_len; ++i) {
        size_t start = g_pmmap[i].addr;
        size_t end = g_pmmap[i].end;
        if ((start <= ptr) && (ptr < end)) {
            return &g_pmmap[i];
        }
    }

    return NULL;
}
