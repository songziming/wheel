# wheel OS — 顶层构建
# 协调 kernel、user 子模块，生成启动镜像

ARCH      ?= x86_64
DEBUG     ?= 1
OUT       := $(CURDIR)/build
ISO_DIR   := $(OUT)/iso
BOOT_DIR  := $(ISO_DIR)/boot/grub

.PHONY: all kernel user iso unit cov clean loc

all: iso

#-------------------------------------------------------------------------------
# 子模块
#-------------------------------------------------------------------------------

USER_DIR := $(OUT)/user

kernel:
	$(MAKE) -C kernel ARCH=$(ARCH) DEBUG=$(DEBUG) BUILD_DIR=$(OUT) \
		EMBED_USER="$(wildcard $(USER_DIR)/*.ko)"

user:
	$(MAKE) -C user BUILD_DIR=$(USER_DIR)

unit:
	$(MAKE) -C kernel unit BUILD_DIR=$(OUT)

cov:
	$(MAKE) -C kernel cov BUILD_DIR=$(OUT)

clean:
	rm -rf $(OUT)

loc:
	find kernel -name "*.S" -o -name "*.c" -o -name "*.h" | xargs wc -l

#-------------------------------------------------------------------------------
# 启动镜像
#-------------------------------------------------------------------------------

# 各行命令使用同一个 shell（gnu-make only）
# 否则写入多行字符串会被拆成多个命令
.ONESHELL:

# kernel 输出 wheel.elf，user 输出 initrd.tar
iso: kernel user
	@rm -rf $(ISO_DIR)
	@mkdir -p $(BOOT_DIR)
	@cat << EOF > $(BOOT_DIR)/grub.cfg
	set default=0
	GRUB_GFXMODE=auto
	menuentry "wheel (multiboot 2, graphical)" {
	    multiboot2 /wheel.elf
	}
	menuentry "wheel (multiboot 1)" {
	    multiboot /wheel.elf
	}
	EOF
	@cp $(OUT)/wheel.elf $(ISO_DIR)/
	@test -f $(USER_DIR)/initrd.tar && cp $(USER_DIR)/initrd.tar $(ISO_DIR)/ || true
	@grub-mkrescue -o $(OUT)/cd.iso $(ISO_DIR)
