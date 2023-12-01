#ifndef CONFIG_H
#define CONFIG_H

#define CFG_BOOT_STACK_SIZE     0x4000
#define CFG_INT_STACK_SIZE      0x4000
#define CFG_KERNEL_STACK_SIZE   0x4000
#define CFG_USER_STACK_SIZE     0x4000

#define CFG_TEMP_ALLOT_SIZE     0x4000
#define CFG_PERM_ALLOT_SIZE     0x5000

#define CFG_DBG_BUFF_SIZE       0x1000
#define CFG_KBD_BUFF_SIZE       128
#define CFG_TTY_BUFF_SIZE       1024

#define CFG_WORK_QUEUE_SIZE     64
#define CFG_SYS_CLOCK_RATE      50
#define CFG_TASK_TIMESLICE      10

#endif // CONFIG_H