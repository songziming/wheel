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

OUT_LIB := $(OUT_DIR)/libwheel.so
OUT_TEST := $(OUT_DIR)/test
OUT_TEST2 := $(OUT_DIR)/test2

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

SFILES := $(shell find $(KSUBDIRS) -name "*.S")
CFILES := $(shell find $(KSUBDIRS) -name "*.c")
XFILES := $(shell find $(KSUBDIRS) -name "*.cc")


# 内核源码

# KSUBDIRS := $(KERNEL)/arch_$(ARCH) memory library
# KSOURCES := $(foreach d,$(KSUBDIRS),$(shell find $(d) -name *.S -o -name *.c))
# KSOURCES := $(shell find $(KSUBDIRS) -name "*.S" -o -name "*.c")
KOBJECTS := $(patsubst %,$(OUT_DIR)/%.ko,$(SFILES) $(CFILES))

# 单元测试文件
# TSOURCES := $(wildcard kernel_test/*.c) $(KSOURCES)
# TOBJECTS := $(patsubst %,$(OUT_DIR)/%.to,$(TSOURCES))

# # 单元测试，内核部分编译成动态库
# T2OBJS := $(patsubst %,$(OUT_DIR)/t2/%.to,$(filter %.c,$(KSOURCES)))
# T3OBJS := $(patsubst %,$(OUT_DIR)/%.to,$(wildcard kernel_test/*.c))


# 单元测试，测试代码使用 C++，放在同目录下，使用 googletest
# TSOURCES := $(shell find $(KSUBDIRS) -name "*.c" -o -name "*.cc")
# TOBJECTS := $(patsubst %,$(OUT_DIR)/%.to,$(TSOURCES))
TCOBJS := $(patsubst %,$(OUT_DIR)/%.to,$(CFILES))
XCOBJS := $(patsubst %,$(OUT_DIR)/%.to,$(XFILES))


# 依赖文件和输出目录
DEPENDS  := $(patsubst %,%.d,$(KOBJECTS) $(TCOBJS) $(XCOBJS))
OBJDIRS  := $(sort $(dir $(DEPENDS)))


#-------------------------------------------------------------------------------
# 编译链接选项
#-------------------------------------------------------------------------------

CFLAGS := -I$(KERNEL) -I$(KERNEL)/arch_$(ARCH) -ffunction-sections -fdata-sections

KCFLAGS := -std=c11 $(CFLAGS) -target $(ARCH)-pc-none-elf -flto
KCFLAGS += -Wall -Wextra -Wshadow -Werror=implicit

KLFLAGS := -T $(KERNEL)/arch_$(ARCH)/layout.ld -Map=$(OUT_MAP)
KLFLAGS += -nostdlib --gc-sections --no-warnings

TCFLAGS := -g $(CFLAGS) -DUNIT_TEST -fsanitize=address
# TXFLAGS := -g $(CFLAGS) -D

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
test2: $(OUT_TEST2)

cov: $(COV_DIR)

clean:
	rm -rf $(OUT_DIR)


#-------------------------------------------------------------------------------
# 构建规则
#-------------------------------------------------------------------------------

# $(KOBJECTS) $(TOBJECTS) $(T2OBJS) $(T3OBJS) $(T4OBJS): | $(OBJDIRS)

$(KOBJECTS) : | $(dir $(KOBJECTS))
$(TCOBJS) : | $(dir $(TCOBJS))
$(XCOBJS) : | $(dir $(XCOBJS))

# $(T2OBJS) : | $(dir $(T2OBJS))
# $(T3OBJS) : | $(dir $(T3OBJS))
# $(T4OBJS) : | $(dir $(T4OBJS))


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


# # 基于动态库的单元测试（便于 mock）
# # $(OUT_DIR)/t2/$(KERNEL)/%.S.to: $(KERNEL)/%.S
# # 	clang -c -DS_FILE -fPIC $(TCFLAGS) $(BAREMETAL) $(MAKECOV) $(MAKEDEP) -o $@ $<
# $(OUT_DIR)/t2/$(KERNEL)/%.c.to: $(KERNEL)/%.c
# 	clang -c -DC_FILE -fPIC $(TCFLAGS) $(BAREMETAL) $(MAKECOV) $(MAKEDEP) -o $@ $<
# $(OUT_LIB): $(T2OBJS)
# 	clang -fuse-ld=lld -fPIC -shared -o $@ $^
# $(OUT_DIR)/kernel_test/%.c.to: kernel_test/%.c
# 	clang -c -DC_FILE $(TCFLAGS) $(MAKEDEP) -o $@ $<
# $(OUT_TEST): $(T3OBJS) | $(OUT_LIB)
# 	clang -fuse-ld=lld $(MAKECOV) -o $@ $^ -lasan -L $(OUT_DIR) -lwheel





# 编译单元测试，使用 C++ 和 googletest 实现的单元测试
$(OUT_DIR)/$(KERNEL)/%.c.to: $(KERNEL)/%.c
	clang -c -DC_FILE -fPIC -std=c11 $(TCFLAGS) $(BAREMETAL) $(MAKECOV) $(MAKEDEP) -o $@ $<
$(OUT_LIB): $(TCOBJS)
	clang -fuse-ld=lld -fPIC -shared -o $@ $^
$(OUT_DIR)/$(KERNEL)/%.cc.to: $(KERNEL)/%.cc
	clang++ -c -DC_FILE -std=c++14 $(TCFLAGS) $(MAKEDEP) -o $@ $<
$(OUT_TEST): $(XCOBJS) | $(OUT_LIB)
	clang++ -fuse-ld=lld $(MAKECOV) -L $(OUT_DIR) -o $@ $^ -lasan -lwheel -lgtest -lgtest_main



# 运行单元测试，生成代码覆盖率文件
$(COV_RAW): $(OUT_TEST)
	LLVM_PROFILE_FILE=$@ $< || true
$(COV_DAT): $(COV_RAW)
	llvm-profdata merge -sparse $< -o $@
$(COV_DIR): $(COV_DAT) | $(OUT_TEST)
	llvm-cov show $(OUT_TEST) -instr-profile=$< -format=html -o $@

-include $(DEPENDS)
