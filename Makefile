# build script for wheel kernel

DEBUG ?= 1
UBSAN ?= $(DEBUG)
KASAN ?= 0 #$(DEBUG)
KTEST ?= $(DEBUG)
KCOV  ?= 1

ARCH  ?= x86_64

# CC := clang --target=$(ARCH)-pc-none-elf
# LD := ld.lld


#-------------------------------------------------------------------------------
# 输出文件路径
#-------------------------------------------------------------------------------

OUT_DIR := build
ISO_DIR := $(OUT_DIR)/iso
OUT_ELF := $(OUT_DIR)/wheel.elf
OUT_MAP := $(OUT_DIR)/wheel.map
OUT_ISO := $(OUT_DIR)/cd.iso
OUT_IMG := $(OUT_DIR)/hd.img
OUT_TEST := $(OUT_DIR)/test


#-------------------------------------------------------------------------------
# 内核文件列表
#-------------------------------------------------------------------------------

KERNEL := kernel_new

KSUBDIRS := $(patsubst %/,%,$(wildcard $(KERNEL)/*/))
KSUBDIRS := $(filter-out $(wildcard $(KERNEL)/arch*), $(KSUBDIRS))
KSUBDIRS := $(KERNEL)/arch_$(ARCH) $(KSUBDIRS)

# 内核源码
KSOURCES := $(foreach d,$(KSUBDIRS),$(shell find $(d) -name "*.S" -o -name "*.c"))
KOBJECTS := $(patsubst %,$(OUT_DIR)/%.ko,$(KSOURCES))

# 单元测试文件
TSOURCES := $(wildcard kernel_test/*.c) $(filter %.c,$(KSOURCES))
TOBJECTS := $(patsubst %,$(OUT_DIR)/%.to,$(TSOURCES))

# 依赖文件和输出目录
DEPENDS  := $(patsubst %,%.d,$(KOBJECTS) $(TOBJECTS))
OBJDIRS  := $(sort $(dir $(DEPENDS)))


#-------------------------------------------------------------------------------
# 编译链接选项
#-------------------------------------------------------------------------------

CFLAGS := -std=c11 $(KSUBDIRS:%=-I%) -ffunction-sections -fdata-sections

STANDALONE := -ffreestanding -fno-builtin
COVERAGE := -fprofile-instr-generate -fcoverage-mapping

# 编译内核用的参数
KCFLAGS := $(CFLAGS) -target $(ARCH)-pc-none-elf -flto -Wall -Wextra -Wshadow -Werror=implicit

# 编译测试代码用的参数
TCFLAGS := $(CFLAGS) -fsanitize=address

KLFLAGS := -nostdlib --gc-sections -Map=$(OUT_MAP) -T $(KERNEL)/arch_$(ARCH)/layout.ld --no-warnings

DEPGEN = -MT $@ -MMD -MP -MF $@.d

ifeq ($(DEBUG),1)
    KCFLAGS += -g -DDEBUG -fstack-protector -fno-omit-frame-pointer
else
    KCFLAGS += -O2 -DNDEBUG
endif

ifeq ($(UBSAN),1)
    KCFLAGS += -fsanitize=undefined
endif

ifeq ($(KASAN),1)
    KCFLAGS += -fsanitize=kernel-address
    KCFLAGS += -mllvm -asan-mapping-offset=0xdfffe00000000000
    KCFLAGS += -mllvm -asan-globals=false
endif

ifeq ($(KCOV),1)
    KCFLAGS += -fprofile-instr-generate -fcoverage-mapping -fcoverage-mcdc
endif

include $(KERNEL)/arch_$(ARCH)/config.mk


#-------------------------------------------------------------------------------
# 构建目标
#-------------------------------------------------------------------------------

.PHONY: all elf iso img test clean

all: elf iso img
elf: $(OUT_ELF)
iso: $(OUT_ISO)
img: $(OUT_IMG)

test: $(OUT_TEST)

clean:
	rm -rf $(OUT_DIR)


#-------------------------------------------------------------------------------
# 构建规则
#-------------------------------------------------------------------------------

$(KOBJECTS) $(TOBJECTS): | $(OBJDIRS)

$(OUT_DIR)/%/:
	mkdir -p $@

$(OUT_DIR)/%.S.ko: %.S
	clang -c -DS_FILE $(KCFLAGS) $(STANDALONE) $(DEPGEN) -o $@ $<

$(OUT_DIR)/%.c.ko: %.c # 内核代码，用于内核
	clang -c -DC_FILE $(KCFLAGS) $(STANDALONE) $(DEPGEN) -o $@ $<

$(OUT_ELF): $(KOBJECTS)
	ld.lld $(KLFLAGS) -o $@ $^

$(OUT_DIR)/$(KERNEL)/%.c.to: $(KERNEL)/%.c # 内核代码，用于单元测试
	clang -c -DC_FILE $(TCFLAGS) $(STANDALONE) $(COVERAGE) $(DEPGEN) -o $@ $<

$(OUT_DIR)/kernel_test/%.c.to: kernel_test/%.c # 单元测试代码，用于单元测试
	clang -c -DC_FILE $(TCFLAGS) $(DEPGEN) -o $@ $<

$(OUT_TEST): $(TOBJECTS)
	clang -fuse-ld=lld -lasan -o $@ $^

$(OUT_ISO): $(OUT_ELF) host_tools/grub.cfg | $(ISO_DIR)/boot/grub/
	cp $(OUT_ELF) $(ISO_DIR)/wheel.elf
	cp host_tools/grub.cfg $(ISO_DIR)/boot/grub/grub.cfg
	grub-mkrescue -o $@ $(ISO_DIR)

$(OUT_IMG): $(OUT_ELF) host_tools/grub.cfg
ifneq ($(shell id -u),0)
	sudo host_tools/mkimage.sh $@
else
	host_tools/mkimage.sh $@
endif
	mmd -i $(OUT_IMG)@@1M -D s ::/boot/grub || true
	mcopy -i $(OUT_IMG)@@1M -D o -nv host_tools/grub.cfg ::/boot/grub/grub.cfg
	mcopy -i $(OUT_IMG)@@1M -D o -nv $(OUT_ELF) ::/wheel.elf

-include $(DEPENDS)
