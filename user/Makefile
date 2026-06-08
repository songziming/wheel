# 用户态程序的编译脚本

CFLAGS := -ffreestanding -nostdlib

APPS := hello3.ko

.PHONY: all clean

all: $(APPS)

clean:
	rm -rf $(APPS)
	rm -rf *.bin

%.bin: %.c | flat.ld
	clang -target x86_64-none-elf $(CFLAGS) -Wl,-Tflat.ld -o $@ $< stub.S

%.ko: %.bin
	objcopy -I binary -O elf64-x86-64 -B i386:x86-64 $< $@
