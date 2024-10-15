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

LIB_NAME := wheel
TEST_LIB := $(OUT_DIR)/lib$(LIB_NAME).so
TEST_BIN := $(OUT_DIR)/test

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

SFILES := $(shell find $(KSUBDIRS) -name "*.S")  # 汇编源码
CFILES := $(shell find $(KSUBDIRS) -name "*.c")  # C 源码
TFILES := $(shell find $(KSUBDIRS) -name "*.cc") # 单元测试代码

KOBJS := $(patsubst %,$(OUT_DIR)/%.ko,$(SFILES) $(CFILES))
LOBJS := $(patsubst %,$(OUT_DIR)/%.to,$(CFILES))
TOBJS := $(patsubst %,$(OUT_DIR)/%.to,$(TFILES))


# 依赖文件和输出目录
DEPENDS  := $(patsubst %,%.d,$(KOBJS) $(LOBJS) $(TOBJS))
OBJDIRS  := $(sort $(dir $(DEPENDS)))


#-------------------------------------------------------------------------------
# 编译链接选项
#-------------------------------------------------------------------------------

CFLAGS := -c -I$(KERNEL) -I$(KERNEL)/arch_$(ARCH) -ffunction-sections -fdata-sections

KCFLAGS := -std=c11 $(CFLAGS) -target $(ARCH)-pc-none-elf -flto
KCFLAGS += -Wall -Wextra -Wshadow -Werror=implicit

KLFLAGS := -T $(KERNEL)/arch_$(ARCH)/layout.ld -Map=$(OUT_MAP)
KLFLAGS += -nostdlib --gc-sections --no-warnings

TCFLAGS := -g $(CFLAGS) -DUNIT_TEST -DC_FILE -fsanitize=address

NOSTD := -ffreestanding -fno-builtin
GENCOV := -fprofile-instr-generate -fcoverage-mapping
GENDEP = -MT $@ -MMD -MP -MF $@.d

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

lib: $(TEST_LIB)
test: $(TEST_BIN)
test2: $(OUT_TEST2)

cov: $(COV_DIR)

clean:
	rm -rf $(OUT_DIR)


#-------------------------------------------------------------------------------
# 构建规则
#-------------------------------------------------------------------------------

$(KOBJS) : | $(dir $(KOBJS))
$(LOBJS) : | $(dir $(LOBJS))
$(TOBJS) : | $(dir $(TOBJS))

$(OUT_DIR)/%/:
	mkdir -p $@

# 编译内核
$(OUT_DIR)/%.S.ko: %.S
	clang -DS_FILE $(KCFLAGS) $(NOSTD) $(GENDEP) -o $@ $<
$(OUT_DIR)/%.c.ko: %.c
	clang -DC_FILE $(KCFLAGS) $(NOSTD) $(GENDEP) -o $@ $<
$(OUT_ELF): $(KOBJS)
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

# 编译单元测试，使用 C++ 和 googletest 实现的单元测试
$(OUT_DIR)/$(KERNEL)/%.c.to: $(KERNEL)/%.c
	clang -std=c11 -fPIC $(TCFLAGS) $(NOSTD) $(GENCOV) $(GENDEP) -o $@ $<
$(TEST_LIB): $(LOBJS)
	clang -fuse-ld=lld $(GENCOV) -shared -o $@ $^
$(OUT_DIR)/$(KERNEL)/%.cc.to: $(KERNEL)/%.cc
	clang++ -std=c++14 $(TCFLAGS) $(GENDEP) -o $@ $<
$(TEST_BIN): $(TOBJS) | $(TEST_LIB)
	clang++ -fuse-ld=lld -L$(OUT_DIR) -o $@ $^ -lasan -l$(LIB_NAME) -lgtest -lgtest_main

# 运行单元测试，生成代码覆盖率文件
$(COV_RAW): $(TEST_BIN)
	LLVM_PROFILE_FILE=$@ LD_LIBRARY_PATH=$(OUT_DIR) $<
$(COV_DAT): $(COV_RAW)
	llvm-profdata merge -sparse $< -o $@
$(COV_DIR): $(COV_DAT) | $(TEST_BIN)
	llvm-cov show $(TEST_LIB) -instr-profile=$< -format=html -o $@

-include $(DEPENDS)
