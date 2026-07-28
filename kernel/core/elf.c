#include "elf.h"
#include "proc.h"

#include <kstring.h>
#include <vmspace.h>
#include <arch_api.h>
#include <debug.h>


// 将 ELF 的 p_flags 转换为 mmu_attr_t
static mmu_attr_t elf_to_mmu_attr(Elf64_Word p_flags) {
    mmu_attr_t attrs = MMU_USER;
    if (p_flags & PF_W) { attrs |= MMU_WRITE; }
    if (p_flags & PF_X) { attrs |= MMU_EXEC; }
    // PF_R 在 x86_64 上总是隐含可读，无需特殊处理
    return attrs;
}


// 如果返回 0，表示加载失败，pid->vm 可能残留一些 segment
size_t elf_load(proc_t *pid, const void *data, size_t len) {
    // 验证文件大小至少能容纳 ELF header
    if (len < sizeof(Elf64_Ehdr)) {
        logk("elf_load: file too small for ELF header\n");
        return 0;
    }

    const Elf64_Ehdr *ehdr = (const Elf64_Ehdr*)data;

    // 验证 ELF 魔数
    if (ehdr->e_ident[EI_MAG0] != ELFMAG0 ||
        ehdr->e_ident[EI_MAG1] != ELFMAG1 ||
        ehdr->e_ident[EI_MAG2] != ELFMAG2 ||
        ehdr->e_ident[EI_MAG3] != ELFMAG3) {
        logk("elf_load: invalid ELF magic\n");
        return 0;
    }

    // 验证 64 位、小端、当前版本
    if (ehdr->e_ident[EI_CLASS] != ELFCLASS64) {
        logk("elf_load: not a 64-bit ELF\n");
        return 0;
    }
    if (ehdr->e_ident[EI_DATA] != ELFDATA2LSB) {
        logk("elf_load: not little-endian\n");
        return 0;
    }
    if (ehdr->e_ident[EI_VERSION] != EV_CURRENT) {
        logk("elf_load: unsupported ELF version\n");
        return 0;
    }

    // 验证是可执行文件、x86_64 架构
    if (ehdr->e_type != ET_EXEC) {
        logk("elf_load: not an executable (type=%d)\n", ehdr->e_type);
        return 0;
    }
    if (ehdr->e_machine != EM_X86_64) {
        logk("elf_load: not x86_64 (machine=%d)\n", ehdr->e_machine);
        return 0;
    }

    // 验证 program header 在文件范围内
    size_t ph_end = (size_t)ehdr->e_phoff + (size_t)ehdr->e_phnum * ehdr->e_phentsize;
    if (ph_end > len || ehdr->e_phnum == 0) {
        logk("elf_load: no program headers or out of bounds\n");
        return 0;
    }

    const char *file_base = (const char*)data;
    int seg_count = 0;

    for (int i = 0; i < ehdr->e_phnum; i++) {
        const Elf64_Phdr *p = (const Elf64_Phdr*)(file_base + ehdr->e_phoff + i * ehdr->e_phentsize);

        // 只关心 PT_LOAD 类型的段、跳过空段
        if ((PT_LOAD != p->p_type) || (0 == p->p_memsz)) {
            continue;
        }

        // 目标虚拟地址应不小于 4K（NULL 页面保护）
        if (p->p_vaddr < PAGE_SIZE) {
            logk("elf_load: segment vaddr 0x%zx too low\n", (size_t)p->p_vaddr);
            return 0;
        }

        // 检查文件数据范围
        if (p->p_offset + p->p_filesz > len) {
            logk("elf_load: segment file data out of bounds\n");
            return 0;
        }

        // 映射段到目标虚拟地址，初始以可写权限映射（拷贝数据用）
        // vmspace_alloc_at 内部会向上取整到页边界
        vmrange_t *rng = proc_valloc(pid, (size_t)p->p_vaddr, (size_t)p->p_memsz, MMU_WRITE);
        if (NULL == rng) {
            logk("elf_load: failed to allocate segment at 0x%zx, size 0x%zx\n",
                (size_t)p->p_vaddr, (size_t)p->p_memsz);
            return 0;
        }
        rng->desc = "elf-load";

        // 从文件拷贝段数据
        // 剩余部分清零（内存大小大于文件大小的部分）
        char *vaddr = (char*)rng->vaddr;
        if (p->p_filesz > 0) {
            kmemcpy(vaddr, file_base + p->p_offset, (size_t)p->p_filesz);
        }
        if (p->p_memsz > p->p_filesz) {
            size_t bss_start = (size_t)p->p_filesz;
            size_t bss_size = (size_t)(p->p_memsz - p->p_filesz);
            kmemset(vaddr + bss_start, 0, bss_size);
        }

        // 根据段标志设置最终页表属性
        // 对于非可写段，移除写权限
        mmu_attr_t final_attrs = elf_to_mmu_attr(p->p_flags);
        vmspace_remap(&pid->vm, rng, final_attrs);

        seg_count++;
    }

    if (0 == seg_count) {
        logk("elf_load: no PT_LOAD segments found\n");
        return 0;
    }

    return (size_t)ehdr->e_entry;
}
