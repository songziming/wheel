#ifndef CONFIG_H
#define CONFIG_H

#define KERNEL_REAL_BASE    0x8000  // 32K 实模式启动代码位置


#define KERNEL_LOAD_ADDR        0x0000000000100000UL    //  1M
#define KERNEL_TEXT_BASE        0xffffffff80000000UL    // -2G
#define AP_REALMODE_ADDR        0x8000                  // 32K

#endif // CONFIG_H
