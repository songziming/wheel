KCFLAGS += -mcmodel=kernel -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -mno-3dnow -mno-fma
KLFLAGS += -z max-page-size=0x1000

# objcopy 参数：将 binary 转成 ELF 目标文件时使用
# TODO 未来将 users.tar 映射到 init.data 里面，启动之后可回收内存
OBJCOPY_ARCH := -O elf64-x86-64 -B i386:x86-64
