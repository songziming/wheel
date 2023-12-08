#include <dev/acpi_base.h>
#include <arch_interface.h>
#include <arch_mem.h>

#include <debug.h>
#include <page.h>
#include <libk.h>


// typedef struct back_tbl {
//     acpi_tbl_t *tbl; // 指向 acpi 表
//     vmrange_t   rng; // 如果表位于可用内存，需要备份
// } back_tbl_t;

static int g_table_count = 0;
static acpi_tbl_t **g_tables = NULL;
// static vmrange_t g_rng_tables;

// static acpi_tbl_t **g_tables = NULL;
// static vmrange_t *g_ranges = NULL;



static INIT_TEXT uint8_t bytes_sum(const void *s, size_t n) {
    const uint8_t *arr = (const uint8_t *)s;
    int sum = 0;
    for (size_t i = 0; i < n; ++i) {
        sum += (int)arr[i];
    }
    return sum & 0xff;
}


// 在内存中搜索 RSDP，找到则返回地址，否则返回 NULL
INIT_TEXT acpi_rsdp_t *acpi_probe_rsdp() {
    const union ptr_sig {
        uint64_t u;
        char     s[8];
    } sig = {
        .s = "RSD PTR ",
    };

    // 获取 EBDA 地址
    uint16_t ebda_base = *(uint16_t *)0x40e;
    size_t   ebda_addr = (size_t)ebda_base << 4;

    // 搜索 EBDA 开头的 1KB，16 字节对齐
    for (size_t addr = ebda_addr; addr < ebda_addr + 1024; addr += 16) {
        if (sig.u == *(uint64_t *)addr) {
            return (acpi_rsdp_t *)addr;
        }
    }

    // 搜索 1M 之前的 BIOS 数据区
    for (size_t addr = 0xe0000; addr < 0x100000; addr += 16) {
        if (sig.u == *(uint64_t *)addr) {
            return (acpi_rsdp_t *)addr;
        }
    }

    return NULL;
}


// 检查子表是否有效，并检查所处内存是否安全
// 如果处于可用内存，则将表备份，否则内存可能被踩
static INIT_TEXT acpi_tbl_t *check_table(uint64_t addr) {
    acpi_tbl_t *tbl = (acpi_tbl_t *)addr;

    if (bytes_sum(tbl, tbl->length)) {
        dbg_print("ACPI::%.4s checksum failed!\n", tbl->signature);
        return NULL;
    }

    if (ram_range_has_overlap((size_t)tbl, tbl->length)) {
        dbg_print("backup ACPI table %.4s\n", tbl->signature);
        acpi_tbl_t *bak = early_const_alloc(tbl->length);
        memcpy(bak, tbl, tbl->length);
        tbl = bak;
    }

    return tbl;
}


// 从 RSDT 提取子表地址
static INIT_TEXT void parse_rsdp_v1(acpi_rsdp_t *rsdp) {
    if (bytes_sum(rsdp, offsetof(acpi_rsdp_t, length))) {
        dbg_print("ACPI::RSDP v1.0 checksum failed!\n");
        return;
    }

    acpi_rsdt_t *rsdt = (acpi_rsdt_t *)(size_t)rsdp->rsdt_addr;
    if (bytes_sum(rsdt, rsdt->header.length)) {
        dbg_print("ACPI::RSDT checksum failed!\n");
        return;
    }

    g_table_count = (rsdt->header.length - sizeof(acpi_tbl_t)) / sizeof(uint32_t);
    g_tables = early_const_alloc(g_table_count * sizeof(acpi_tbl_t *));

    for (int i = 0; i < g_table_count; ++i) {
        g_tables[i] = check_table(rsdt->entries[i]);
    }
}


// 从 XSDT 提取子表地址
static INIT_TEXT void parse_rsdp_v2(acpi_rsdp_t *rsdp) {
    if (bytes_sum(rsdp, rsdp->length)) {
        dbg_print("ACPI::RSDP v2.0 checksum failed!\n");
        return;
    }

    acpi_xsdt_t *xsdt = (acpi_xsdt_t *)rsdp->xsdt_addr;
    if (bytes_sum(xsdt, xsdt->header.length)) {
        dbg_print("ACPI::XSDT checksum failed!\n");
        return;
    }

    g_table_count = (xsdt->header.length - sizeof(acpi_tbl_t)) / sizeof(uint64_t);
    g_tables = early_const_alloc(g_table_count * sizeof(acpi_tbl_t *));

    for (int i = 0; i < g_table_count; ++i) {
        g_tables[i] = check_table(xsdt->entries[i]);
    }
}


INIT_TEXT void acpi_parse_rsdp(acpi_rsdp_t *rsdp) {
    if (0 == rsdp->revision) {
        parse_rsdp_v1(rsdp);
    } else {
        parse_rsdp_v2(rsdp);
    }
}


// 根据签名寻找 ACPI 表
acpi_tbl_t *acpi_get_table(const char sig[4]) {
    for (int i = 0; i < g_table_count; ++i) {
        if (NULL == g_tables[i]) {
            continue;
        }
        if (0 == strncmp(sig, g_tables[i]->signature, 4)) {
            return g_tables[i];
        }
    }
    return NULL;
}
