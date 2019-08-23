#include <wheel.h>

#define ELF_WRONG_MAGIC     ((int) 2)
#define ELF_WRONG_ARCH      ((int) 3)
#define ELF_WRONG_TYPE      ((int) 4)
#define ELF_NO_SEG_TBL      ((int) 5)

int elf64_load(u8 * bin, usize len) {
    elf64_hdr_t * hdr = (elf64_hdr_t *) bin;

    if ((sizeof(elf64_hdr_t) > len) ||
        (hdr->e_ident[0] != 0x7f)   ||
        (hdr->e_ident[1] != 'E')    ||
        (hdr->e_ident[2] != 'L')    ||
        (hdr->e_ident[3] != 'F')) {
        return ELF_WRONG_MAGIC;
    }

    if (
#if ARCH == x86_64
        (hdr->e_ident[4] != 2)      ||  // 64-bit
        (hdr->e_ident[5] != 1)      ||  // little endian
        (hdr->e_machine  != EM_AMD64)   // x86_64
#else
    #error "arch not supported"
#endif
    ) {
        return ELF_WRONG_ARCH;
    }

    switch (hdr->e_type) {
    case ET_REL:        // *.o
        break;
    case ET_EXEC:       // *.app
        break;
    case ET_DYN:        // *.so
        break;
    default:
        return ELF_WRONG_TYPE;
    }

    // get program header table's offset
    if ((hdr->e_phoff     ==   0) ||
        (hdr->e_phnum     ==   0) ||
        (hdr->e_phentsize ==   0) ||
        (hdr->e_phoff     >= len) ||
        (hdr->e_phoff + hdr->e_phentsize * hdr->e_phnum >= len)) {
        return ELF_NO_SEG_TBL;
    }

    // loop through each segment, make sure vmrange is usable
    for (int i = 0; i < hdr->e_phnum; ++i) {
        elf64_phdr_t * phdr = (elf64_phdr_t *) (bin + hdr->e_phoff + i * hdr->e_phentsize);
        if (PT_LOAD == phdr->p_type) {
            usize vm_start = ROUND_DOWN(phdr->p_vaddr, PAGE_SIZE);
            usize vm_end   = ROUND_UP(phdr->p_vaddr + phdr->p_memsz, PAGE_SIZE);
            // TODO: check whether this range is usable in the vmspace
        }
    }

    return OK;
}
