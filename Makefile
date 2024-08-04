# build script for wheel kernel

DEBUG ?= 1
UBSAN ?= $(DEBUG)
KASAN ?= 0 #$(DEBUG)
KTEST ?= $(DEBUG)
KCOV  ?= 1

ARCH  ?= x86_64

CC := clang --target=$(ARCH)-pc-none-elf
LD := ld.lld


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
KOBJECTS := $(patsubst %,$(OUT_DIR)/%.o,$(KSOURCES))

# 单元测试文件
TSOURCES := $(wildcard kernel_test/*.c)
TOBJECTS := $(patsubst %,$(OUT_DIR)/%.o,$(TSOURCES))

# 依赖文件和输出目录
DEPENDS  := $(patsubst %.o,%.d,$(KOBJECTS) $(TOBJECTS))
OBJDIRS  := $(sort $(dir $(DEPENDS)))


#-------------------------------------------------------------------------------
# 内核编译选项
#-------------------------------------------------------------------------------

CFLAGS := -std=c11 $(KSUBDIRS:%=-I%) -Wall -Wextra -Wshadow -Werror=implicit
CFLAGS += -ffreestanding -fno-builtin -flto -ffunction-sections -fdata-sections

ifeq ($(DEBUG),1)
    CFLAGS += -g -DDEBUG -fstack-protector -fno-omit-frame-pointer
else
    CFLAGS += -O2 -DNDEBUG
endif

ifeq ($(UBSAN),1)
    CFLAGS += -fsanitize=undefined
endif

ifeq ($(KASAN),1)
    CFLAGS += -fsanitize=kernel-address
    CFLAGS += -mllvm -asan-mapping-offset=0xdfffe00000000000
    CFLAGS += -mllvm -asan-globals=false
endif

LAYOUT := $(KERNEL)/arch_$(ARCH)/layout.ld
LFLAGS := -nostdlib --gc-sections -Map=$(OUT_MAP) -T $(LAYOUT) --no-warnings

ifeq ($(KCOV),1)
    CFLAGS += -fprofile-instr-generate -fcoverage-mapping -fcoverage-mcdc
    # LFLAGS += -fprofile-instr-generate -fcoverage-mapping -fcoverage-mcdc
endif

DEPGEN = -MT $@ -MMD -MP -MF $(patsubst %.o,%.d,$@)

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

$(OUT_DIR)/%.S.o: %.S
	$(CC) -c -DS_FILE $(CFLAGS) $(DEPGEN) -o $@ $<

$(OUT_DIR)/$(KERNEL)/%.c.o: $(KERNEL)/%.c
	$(CC) -c -DC_FILE $(CFLAGS) $(DEPGEN) -o $@ $<

# TODO 编译选项与内核有共用的部分，可以替换成变量
$(OUT_DIR)/kernel_test/%.c.o: kernel_test/%.c
	clang -c -DC_FILE -std=c11 $(KSUBDIRS:%=-I%) $(DEPGEN) -o $@ $<

$(OUT_ELF): $(KOBJECTS)
	$(LD) $(LFLAGS) -o $@ $^

$(OUT_TEST): $(filter %.c.o,$(KOBJECTS)) $(TOBJECTS)
	clang -flto -fuse-ld=lld -o $@ $^

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
