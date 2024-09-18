#include "acpi.h"
#include <arch_impl.h>
#include <memory/pmlayout.h>
#include <library/debug.h>
#include <memory/early_alloc.h>
#include <library/string.h>



typedef struct acpi_rsdp {
    uint64_t    signature;
    uint8_t     checksum;
    char        oem_id[6];
    uint8_t     revision;
    uint32_t    rsdt_addr;

    // 下面的字段属于 v2.0
    uint32_t    length;
    uint64_t    xsdt_addr;
    uint8_t     checksum2;
    uint8_t     reserved[3];
} PACKED acpi_rsdp_t;

typedef struct acpi_rsdt {
    acpi_tbl_t  header;
    uint32_t    entries[0];
} PACKED acpi_rsdt_t;

typedef struct acpi_xsdt {
    acpi_tbl_t  header;
    uint64_t    entries[0];
} PACKED acpi_xsdt_t;

// TODO 可以改为双链表，不需要提前确定表个数
static CONST int g_table_num = 0;
static CONST acpi_tbl_t **g_tables = NULL;








// 搜索 RSDP，找到则返回物理地址，未找到则返回零
INIT_TEXT size_t acpi_probe_rsdp() {
    const union ptr_sig {
        uint64_t u;
        char     s[8];
    } sig = {
        .s = "RSD PTR ",
    };

    // 获取 EBDA 地址
    uint16_t ebda_base = *(uint16_t *)(DIRECT_MAP_ADDR + 0x40e);
    size_t   ebda_addr = (size_t)ebda_base << 4;

    // 搜索 EBDA 开头的 1KB，16 字节对齐
    for (size_t addr = ebda_addr; addr < ebda_addr + 1024; addr += 16) {
        if (sig.u == *(uint64_t *)(DIRECT_MAP_ADDR + addr)) {
            return addr;
        }
    }

    // 搜索 1M 之前的 BIOS 数据区
    for (size_t addr = 0xe0000; addr < 0x100000; addr += 16) {
        if (sig.u == *(uint64_t *)(DIRECT_MAP_ADDR + addr)) {
            return addr;
        }
    }

    return 0;
}




// 内存校验
static INIT_TEXT uint8_t bytes_sum(const void *s, size_t n) {
    const uint8_t *arr = (const uint8_t *)s;
    int sum = 0;
    for (size_t i = 0; i < n; ++i) {
        sum += (int)arr[i];
    }
    return sum & 0xff;
}

// 检查子表是否有效，并检查所处内存是否安全
// 如果处于可用内存，则将表备份，防止内存被踩
// 如果位于安全内存，则映射到 higher-half
static INIT_TEXT acpi_tbl_t *check_table(uint64_t addr) {
    acpi_tbl_t *tbl = (acpi_tbl_t *)(DIRECT_MAP_ADDR + addr);
    if (bytes_sum(tbl, tbl->length)) {
        log("warning: ACPI-%.4s checksum failed!\n", tbl->signature);
        return NULL;
    }

    pmrange_t *pmr = pmrange_at_addr(addr);
    if (!pmr || pmr->type == PM_RESERVED) {
        return tbl;
    }

    acpi_tbl_t *bak = early_alloc_ro(tbl->length);
    memcpy(bak, tbl, tbl->length);
    return bak;
}

// 从 RSDT 提取子表地址
static INIT_TEXT void parse_rsdp_v1(acpi_rsdp_t *rsdp) {
    if (bytes_sum(rsdp, offsetof(acpi_rsdp_t, length))) {
        log("warning: ACPI-RSDP v1.0 checksum failed!\n");
        return;
    }

    acpi_rsdt_t *rsdt = (acpi_rsdt_t *)(size_t)rsdp->rsdt_addr;
    if (bytes_sum(rsdt, rsdt->header.length)) {
        log("warning: ACPI-RSDT checksum failed!\n");
        return;
    }

    g_table_num = (rsdt->header.length - sizeof(acpi_tbl_t)) / sizeof(uint32_t);
    size_t arr_size = (g_table_num + 2) * sizeof(acpi_tbl_t *); // 留出 FACS/DSDT 的位置
    g_tables = early_alloc_ro(arr_size);
    memset(g_tables, 0, arr_size);

    for (int i = 0; i < g_table_num; ++i) {
        g_tables[i] = check_table(rsdt->entries[i]);
    }
}

// 从 XSDT 提取子表地址
static INIT_TEXT void parse_rsdp_v2(acpi_rsdp_t *rsdp) {
    if (bytes_sum(rsdp, rsdp->length)) {
        log("warning: ACPI-RSDP v2.0 checksum failed!\n");
        return;
    }

    acpi_xsdt_t *xsdt = (acpi_xsdt_t *)rsdp->xsdt_addr;
    if (bytes_sum(xsdt, xsdt->header.length)) {
        log("warning: ACPI-XSDT checksum failed!\n");
        return;
    }

    g_table_num = (xsdt->header.length - sizeof(acpi_tbl_t)) / sizeof(uint64_t);
    size_t arr_size = (g_table_num + 2) * sizeof(acpi_tbl_t *); // 留出 FACS/DSDT 的位置
    g_tables = early_alloc_ro(arr_size);
    memset(g_tables, 0, arr_size);

    for (int i = 0; i < g_table_num; ++i) {
        g_tables[i] = check_table(xsdt->entries[i]);
    }
}

// 解析 RSDP 引用的 RSDT 或 XSDT
INIT_TEXT void acpi_parse_rsdp(size_t addr) {
    ASSERT(0 == g_table_num);
    ASSERT(NULL == g_tables);
    ASSERT(0 != addr);

    acpi_rsdp_t *rsdp = (acpi_rsdp_t *)(DIRECT_MAP_ADDR + addr);
    // g_acpi_revision = rsdp->revision;
    if (0 == rsdp->revision) {
        parse_rsdp_v1(rsdp);
    } else {
        parse_rsdp_v2(rsdp);
    }

    // // FADT 需要特殊处理
    // fadt_t *fadt = (fadt_t *)acpi_get_table("FACP");
    // if (NULL == fadt) {
    //     return;
    // }

    // uint64_t facs = fadt->firmware_ctrl;
    // if (0 == facs) {
    //     facs = fadt->X_FirmwareControl;
    // }
    // g_tables[g_table_num++] = check_table(facs);

    // uint64_t dsdt = fadt->dsdt;
    // if (0 == dsdt) {
    //     dsdt = fadt->X_Dsdt;
    // }
    // g_tables[g_table_num++] = check_table(dsdt);
}

// 根据签名寻找 ACPI 表
acpi_tbl_t *acpi_table_find(const char sig[4]) {
    ASSERT(0 != g_table_num);
    ASSERT(NULL != g_tables);

    for (int i = 0; i < g_table_num; ++i) {
        if (NULL == g_tables[i]) {
            continue;
        }
        if (0 == memcmp(sig, g_tables[i]->signature, 4)) {
            return g_tables[i];
        }
    }
    return NULL;
}

// 打印所有 ACPI 表
void acpi_tables_show() {
    ASSERT(0 != g_table_num);
    ASSERT(NULL != g_tables);

    log("ACPI tables:\n");
    for (int i = 0; i < g_table_num; ++i) {
        if (NULL != g_tables[i]) {
            log("  - acpi %.4s\n", g_tables[i]->signature);
        }
    }
}
