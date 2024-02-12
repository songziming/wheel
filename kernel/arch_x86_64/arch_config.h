#ifndef ARCH_X86_64_CONFIG_H
#define ARCH_X86_64_CONFIG_H

//------------------------------------------------------------------------------
// 基本信息
//------------------------------------------------------------------------------

#define AP_BOOT_MAGIC       0xdeadbeef

#define PAGE_SIZE           0x1000
#define PAGE_SHIFT          12

#define TIMER_FREQ          2


//------------------------------------------------------------------------------
// 内存布局安排
//------------------------------------------------------------------------------

#define KERNEL_LOAD_ADDR    0x0000000000100000UL //  1M
#define KERNEL_TEXT_ADDR    0xffffffff80000000UL // -2G
#define KERNEL_REAL_ADDR    0x8000  // 32K，实模式启动代码位置

#define DIRECT_MAP_ADDR     0xffff800000000000UL // 物理内存映射地址（共 16TB）
#define STACK_AREA_ADDR     0xffff900000000000UL // 任务栈映射范围（共 16TB）
#define STACK_AREA_END      0xffffa00000000000UL


//------------------------------------------------------------------------------
// 中断向量号
//------------------------------------------------------------------------------

#define VEC_GSI_BASE        0x40    // 外部中断起始编号

#define VEC_LINUX_SYSCALL   0x80    // Linux 系统调用兼容层

#define VEC_IPI_RESCHED     0xd0
#define VEC_IPI_STOPALL     0xd1

#define VEC_LOAPIC_TIMER    0xe0

#define VEC_LOAPIC_ERROR    0xfe
#define VEC_LOAPIC_SPURIOUS 0xff    // spurious 向量号最后 4bit 必须是 f


//------------------------------------------------------------------------------
// 预留的缓冲区大小
//------------------------------------------------------------------------------

#define EARLY_RO_SIZE       0x4000      // 只读预留内存
#define EARLY_RW_SIZE       0x2000      // 读写预留内存

#define KEYBOARD_BUFF_LEN   64          // 按键码缓冲区
#define KERNEL_HEAP_SIZE    0x4000      // 内核堆


//------------------------------------------------------------------------------
// 栈尺寸
//------------------------------------------------------------------------------

#define INIT_STACK_SIZE     0x1000      // 初始化阶段使用的栈
#define INT_STACK_SIZE      0x1000      // 中断栈大小，也是异常栈 IST

// 任务栈也用于中断，栈大小需要能容下 arch_regs_t

#define TASK_STACK_RANK     1       // 任务内核栈的默认大小
#define IDLE_STACK_RANK     0       // 空闲任务内核栈大小

#endif // ARCH_X86_64_CONFIG_H
