#ifndef ELF_H
#define ELF_H

#include <stddef.h>
#include <stdint.h>

//------------------------------------------------------------------------------
// ELF 类型定义（仅 64 位，内核只需要支持 x86_64）
//------------------------------------------------------------------------------

typedef uint16_t Elf64_Half;
typedef uint32_t Elf64_Word;
typedef  int32_t Elf64_Sword;
typedef uint64_t Elf64_Xword;
typedef  int64_t Elf64_Sxword;
typedef uint64_t Elf64_Addr;
typedef uint64_t Elf64_Off;

//------------------------------------------------------------------------------
// ELF 文件头
//------------------------------------------------------------------------------

#define EI_NIDENT 16

typedef struct {
    unsigned char   e_ident[EI_NIDENT]; // 魔数及其他信息
    Elf64_Half      e_type;             // 目标文件类型
    Elf64_Half      e_machine;          // 目标架构
    Elf64_Word      e_version;          // 目标文件版本
    Elf64_Addr      e_entry;            // 入口点虚拟地址
    Elf64_Off       e_phoff;            // Program header 在文件中的偏移
    Elf64_Off       e_shoff;            // Section header 在文件中的偏移
    Elf64_Word      e_flags;            // 处理器特定标志
    Elf64_Half      e_ehsize;           // ELF 头大小
    Elf64_Half      e_phentsize;        // Program header 条目大小
    Elf64_Half      e_phnum;            // Program header 条目数量
    Elf64_Half      e_shentsize;        // Section header 条目大小
    Elf64_Half      e_shnum;            // Section header 条目数量
    Elf64_Half      e_shstrndx;         // Section 名称字符串表索引
} Elf64_Ehdr;

// e_ident 字段索引
#define EI_MAG0     0
#define EI_MAG1     1
#define EI_MAG2     2
#define EI_MAG3     3
#define EI_CLASS    4
#define EI_DATA     5
#define EI_VERSION  6

// ELF 魔数
#define ELFMAG0     0x7f
#define ELFMAG1     'E'
#define ELFMAG2     'L'
#define ELFMAG3     'F'

// e_ident[EI_CLASS] 取值
#define ELFCLASS64  2

// e_ident[EI_DATA] 取值
#define ELFDATA2LSB 1

// e_type 取值
#define ET_EXEC     2   // 可执行文件

// e_machine 取值
#define EM_X86_64   62  // AMD x86-64

// e_version 取值
#define EV_CURRENT  1

//------------------------------------------------------------------------------
// Program header
//------------------------------------------------------------------------------

typedef struct {
    Elf64_Word  p_type;     // 段类型
    Elf64_Word  p_flags;    // 段标志
    Elf64_Off   p_offset;   // 段在文件中的偏移
    Elf64_Addr  p_vaddr;    // 段的虚拟地址
    Elf64_Addr  p_paddr;    // 段的物理地址（未使用）
    Elf64_Xword p_filesz;   // 段在文件中的大小
    Elf64_Xword p_memsz;    // 段在内存中的大小
    Elf64_Xword p_align;    // 段对齐
} Elf64_Phdr;

// p_type 取值
#define PT_LOAD     1   // 可加载的段

// p_flags 取值
#define PF_X        (1 << 0)   // 可执行
#define PF_W        (1 << 1)   // 可写
#define PF_R        (1 << 2)   // 可读

//------------------------------------------------------------------------------
// ELF 加载函数
//------------------------------------------------------------------------------

// struct proc;
typedef struct proc proc_t;

// 将静态链接的 ELF 可执行文件加载到进程的地址空间中
// 调用前需要先 task_enter_process(pid)，使得当前地址空间为目标进程的页表
// 成功返回入口点虚拟地址，失败返回 0
size_t elf_load(proc_t *pid, const void *data, size_t len);

#endif // ELF_H
