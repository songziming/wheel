// 中断异常和系统调用的入口

.global isr_entries

.section ".init.data", "aw"

isr_entries:
    .long   0
