# build script for wheel kernel

DEBUG ?= 1
UBSAN ?= $(DEBUG)
KASAN ?= 0 #$(DEBUG)
KTEST ?= $(DEBUG)
KCOV  ?= 1

ARCH  ?= x86_64


#-------------------------------------------------------------------------------
# 输出文件路径
#-------------------------------------------------------------------------------

OUT_DIR := build
ISO_DIR := $(OUT_DIR)/iso
OUT_ELF := $(OUT_DIR)/wheel.elf
OUT_MAP := $(OUT_DIR)/wheel.map
OUT_ISO := $(OUT_DIR)/cd.iso
OUT_IMG := $(OUT_DIR)/hd.img
OUT_LIB := $(OUT_DIR)/wheel.so
OUT_TEST := $(OUT_DIR)/test

COV_DIR := $(OUT_DIR)/coverage
COV_RAW := $(OUT_DIR)/test.profraw
COV_DAT := $(OUT_DIR)/test.profdata


#-------------------------------------------------------------------------------
# 内核文件列表
#-------------------------------------------------------------------------------

KERNEL := kernel_new
# ARCH_DIR := $(KERNEL)/arch_$(ARCH)

KSUBDIRS := $(patsubst %/,%,$(wildcard $(KERNEL)/*/))
KSUBDIRS := $(filter-out $(wildcard $(KERNEL)/arch*), $(KSUBDIRS))
KSUBDIRS := $(KERNEL)/arch_$(ARCH) $(KSUBDIRS)

# 内核源码
# KSUBDIRS := $(KERNEL)/arch_$(ARCH) memory library
KSOURCES := $(foreach d,$(KSUBDIRS),$(shell find $(d) -name "*.S" -o -name "*.c"))
KOBJECTS := $(patsubst %,$(OUT_DIR)/%.ko,$(KSOURCES))

# 单元测试文件
# TSOURCES := $(wildcard kernel_test/*.c) $(filter %.c,$(KSOURCES))
TSOURCES := $(wildcard kernel_test/*.c) $(KSOURCES)
TOBJECTS := $(patsubst %,$(OUT_DIR)/%.to,$(TSOURCES))

# 内核代码，编译成动态库，用于单元测试
T2OBJS := $(patsubst %,$(OUT_DIR)/t2/%.to,$(filter %.c,$(KSOURCES)))

# 依赖文件和输出目录
DEPENDS  := $(patsubst %,%.d,$(KOBJECTS) $(TOBJECTS) $(T2OBJS))
OBJDIRS  := $(sort $(dir $(DEPENDS)))


#-------------------------------------------------------------------------------
# 编译链接选项
#-------------------------------------------------------------------------------

CFLAGS := -std=c11 -I$(KERNEL) -I$(KERNEL)/arch_$(ARCH) -ffunction-sections -fdata-sections

KCFLAGS := $(CFLAGS) -target $(ARCH)-pc-none-elf -flto
KCFLAGS += -Wall -Wextra -Wshadow -Werror=implicit

KLFLAGS := -T $(KERNEL)/arch_$(ARCH)/layout.ld -Map=$(OUT_MAP)
KLFLAGS += -nostdlib --gc-sections --no-warnings

TCFLAGS := $(CFLAGS) -g -DUNIT_TEST -fsanitize=address
TLFLAGS := -Wl,--gc-sections

BAREMETAL := -ffreestanding -fno-builtin
MAKECOV := -fprofile-instr-generate -fcoverage-mapping
MAKEDEP = -MT $@ -MMD -MP -MF $@.d

ifeq ($(DEBUG),1)
    KCFLAGS += -g -gdwarf-5 -DDEBUG -fstack-protector -fno-omit-frame-pointer
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

.PHONY: all elf iso img test cov clean

all: elf iso img
elf: $(OUT_ELF)
iso: $(OUT_ISO)
img: $(OUT_IMG)

lib: $(OUT_LIB)
test: $(OUT_TEST)

cov: $(COV_DIR)

clean:
	rm -rf $(OUT_DIR)


#-------------------------------------------------------------------------------
# 构建规则
#-------------------------------------------------------------------------------

$(KOBJECTS) $(TOBJECTS) $(T2OBJS): | $(OBJDIRS)

$(OUT_DIR)/%/:
	mkdir -p $@

# 编译内核
$(OUT_DIR)/%.S.ko: %.S
	clang -c -DS_FILE $(KCFLAGS) $(BAREMETAL) $(MAKEDEP) -o $@ $<
$(OUT_DIR)/%.c.ko: %.c
	clang -c -DC_FILE $(KCFLAGS) $(BAREMETAL) $(MAKEDEP) -o $@ $<
$(OUT_ELF): $(KOBJECTS)
	ld.lld $(KLFLAGS) -o $@ $^

# 生成引导介质
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

# 编译单元测试
$(OUT_DIR)/$(KERNEL)/%.S.to: $(KERNEL)/%.S
	clang -c -DS_FILE $(TCFLAGS) $(BAREMETAL) $(MAKECOV) $(MAKEDEP) -o $@ $<
$(OUT_DIR)/$(KERNEL)/%.c.to: $(KERNEL)/%.c
	clang -c -DC_FILE $(TCFLAGS) $(BAREMETAL) $(MAKECOV) $(MAKEDEP) -o $@ $<
$(OUT_DIR)/kernel_test/%.c.to: kernel_test/%.c
	clang -c -DC_FILE $(TCFLAGS) $(MAKEDEP) -o $@ $<
$(OUT_TEST): $(TOBJECTS)
	clang -fuse-ld=lld $(TLFLAGS) $(MAKECOV) -o $@ $^ -lasan

# 基于动态库的单元测试（方便函数 mock）
# $(OUT_DIR)/t2/$(KERNEL)/%.S.to: $(KERNEL)/%.S
# 	clang -c -DS_FILE -fPIC $(TCFLAGS) $(BAREMETAL) $(MAKECOV) $(MAKEDEP) -o $@ $<
$(OUT_DIR)/t2/$(KERNEL)/%.c.to: $(KERNEL)/%.c
	clang -c -DC_FILE -fPIC $(TCFLAGS) $(BAREMETAL) $(MAKECOV) $(MAKEDEP) -o $@ $<
$(OUT_LIB): $(T2OBJS)
	clang -fPIC -shared -o $@ $^

# 运行单元测试，生成代码覆盖率文件
$(COV_RAW): $(OUT_TEST)
	LLVM_PROFILE_FILE=$@ $< || true
$(COV_DAT): $(COV_RAW)
	llvm-profdata merge -sparse $< -o $@
$(COV_DIR): $(COV_DAT) | $(OUT_TEST)
	llvm-cov show $(OUT_TEST) -instr-profile=$< -format=html -o $@

-include $(DEPENDS)
