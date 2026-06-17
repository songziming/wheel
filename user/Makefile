# user/Makefile — 编译用户态程序

BUILD_DIR ?= build

CC       := $(TOOLCHAIN)clang
CFLAGS   := -target x86_64-none-elf -ffreestanding -nostdlib
CFLAGS   += -fno-stack-protector -mgeneral-regs-only

BIN_DIR  := $(BUILD_DIR)/bin
PROGRAMS := hello3

# 嵌入内核的 .ko 文件（objcopy 格式）
KO_FILES := $(PROGRAMS:%=$(BUILD_DIR)/%.ko)

# 标准 ELF 文件（后续 initrd 使用）
ELF_FILES := $(PROGRAMS:%=$(BIN_DIR)/%.elf)
INITRD    := $(BUILD_DIR)/initrd.tar

.PHONY: all clean

all: $(KO_FILES)

clean:
	rm -rf $(BUILD_DIR)

# flat binary（中间产物）
$(BUILD_DIR)/%.bin: %.c stub.S flat.ld | $(BUILD_DIR)
	$(CC) $(CFLAGS) -Wl,-Tflat.ld -o $@ $< stub.S

# 嵌入内核的目标文件
# cd 到 .bin 所在目录，让 objcopy 只看到 basename，防止 ELF 符号里面带有路径
$(BUILD_DIR)/%.ko: $(BUILD_DIR)/%.bin
	cd $(<D) && objcopy -I binary -O elf64-x86-64 -B i386:x86-64 $(<F) $(abspath $@)

# 标准 ELF（后续 initrd 使用）
$(BIN_DIR)/%.elf: %.c stub.S flat.ld | $(BIN_DIR)
	$(CC) $(CFLAGS) -Wl,-Tflat.ld -o $@ $< stub.S

$(BIN_DIR):
	mkdir -p $@

$(BUILD_DIR):
	mkdir -p $@

# 打包 initrd
$(INITRD): $(ELF_FILES)
	tar cf $@ -C $(BIN_DIR) $(PROGRAMS:=.elf)
