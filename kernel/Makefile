# kernel/Makefile — 编译 wheel 内核和单元测试
# 可被顶层 Makefile 调用，也可单独使用

ARCH    ?= x86_64
DEBUG   ?= 1
OUTDIR  ?= build

# toolchain
KCC := $(TOOLCHAIN)clang
KLD := $(TOOLCHAIN)ld.lld
TCC := $(TOOLCHAIN)clang
TXX := $(TOOLCHAIN)clang++

# host-dependent tools
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
	GRUB_MKRESCUE = i686-elf-grub-mkrescue
	LLVM_PROFDATA = xcrun llvm-profdata
	LLVM_COV      = xcrun llvm-cov
else
	GRUB_MKRESCUE = grub-mkrescue
	LLVM_PROFDATA = llvm-profdata
	LLVM_COV      = llvm-cov
endif

# output files
OUT_ELF  := $(OUTDIR)/wheel.elf
OUT_MAP  := $(OUTDIR)/wheel.map
UNIT_LIB := $(OUTDIR)/libwheel.so
UNIT_BIN := $(OUTDIR)/unit
UNIT_RAW := $(OUTDIR)/unit.profraw
UNIT_DAT := $(OUTDIR)/unit.profdata
UNIT_COV := $(OUTDIR)/coverage

# source files and objects
KERNEL := .
KDIRS  := arch_$(ARCH) core debug lib mem services
AFILES := $(shell find $(KDIRS) -name "*.S")
CFILES := $(shell find $(KDIRS) -name "*.c")
XFILES := $(shell find $(KDIRS) -name "*.cc")

KOBJS := $(patsubst %,$(OUTDIR)/%.ko,$(AFILES) $(CFILES))
LOBJS := $(patsubst %,$(OUTDIR)/%.to,$(CFILES))
TOBJS := $(patsubst %,$(OUTDIR)/%.to,$(XFILES))

ALLOBJS := $(KOBJS) $(LOBJS) $(TOBJS)
OBJDIRS := $(sort $(dir $(ALLOBJS)))
OBJDEPS := $(patsubst %,%.d,$(ALLOBJS))


#-------------------------------------------------------------------------------
# 编译链接选项
#-------------------------------------------------------------------------------

KINC   := -I$(KERNEL)/api $(KDIRS:%=-I$(KERNEL)/%)
NOSTD  := -ffreestanding -fno-builtin -nostdlib
ASAN   := -fsanitize=address
GENCOV := -fprofile-instr-generate -fcoverage-mapping
GENDEP  = -MT $@ -MMD -MP -MF $@.d

# 内核编译选项，C & ASM
KCFLAGS := -c -std=c11 -target $(ARCH)-none-elf $(KINC) $(NOSTD)
KCFLAGS += -ffunction-sections -fdata-sections -fvisibility=hidden #-flto
KCFLAGS += -Wall -Wextra -Wshadow -Werror=implicit -fstack-usage
ifeq ($(DEBUG),1)
	KCFLAGS += -DDEBUG -g -fno-omit-frame-pointer -fstack-protector-strong
	KCFLAGS += -DLOCKDEP
else
	KCFLAGS += -DNDEBUG -O2
endif

# 内核链接选项
KLFLAGS := -T $(KERNEL)/arch_$(ARCH)/layout.ld -Map=$(OUT_MAP)
KLFLAGS += -nostdlib --gc-sections --no-warnings

# 内核库编译选项，生成单元测试用到的动态库
LCFLAGS := -c -std=c11 -g -fPIC -DDEBUG -DUNIT_TEST
LCFLAGS += $(KINC) $(NOSTD) $(GENCOV) $(ASAN)

# 内核库链接选项
LLFLAGS := -shared $(GENCOV) $(ASAN)
ifeq ($(UNAME_S),Darwin)
	LLFLAGS += -Wl,-undefined,dynamic_lookup,-flat_namespace
else ifeq ($(UNAME_S),Linux)
	LLFLAGS += -Wl,--allow-shlib-undefined -fsanitize=address
endif

# 单元测试代码的编译选项，C++
TXFLAGS := -c -std=c++17 -g -DDEBUG -DUNIT_TEST $(KINC)
TXFLAGS += -isystem -pthread $(ASAN)

# 单元测试链接选项
TLFLAGS := -lgtest -lgtest_main $(ASAN)

# 嵌入的用户态程序 .ko 文件（由 user/Makefile 生成）
EMBED_USER ?=

include $(KERNEL)/arch_$(ARCH)/config.mk


#-------------------------------------------------------------------------------
# 构建规则
#-------------------------------------------------------------------------------

.PHONY: all kernel unit cov clean

all: kernel

kernel: $(OUT_ELF)

unit: $(UNIT_BIN)

clean:
	rm -rf $(OUTDIR)

# 创建目标文件所在目录
$(ALLOBJS) : | $(OBJDIRS)
$(OBJDIRS):
	mkdir -p $@

# 编译内核
$(OUTDIR)/%.S.ko: $(KERNEL)/%.S
	$(KCC) $(KCFLAGS) $(GENDEP) -DS_FILE -o $@ $<
$(OUTDIR)/%.c.ko: $(KERNEL)/%.c
	$(KCC) $(KCFLAGS) $(GENDEP) -DC_FILE -o $@ $<
$(OUT_ELF): $(KOBJS) $(EMBED_USER)
	$(KLD) $(KLFLAGS) -o $@ $^

# 内核库，单元测试用，只包括 C 代码，使用默认工具链
$(OUTDIR)/%.c.to: $(KERNEL)/%.c
	$(TCC) $(LCFLAGS) $(GENDEP) -DC_FILE -o $@ $<
$(UNIT_LIB): $(LOBJS)
	$(TXX) $(LLFLAGS) -o $@ $^

# 编译单元测试程序，使用默认工具链
$(OUTDIR)/%.cc.to: $(KERNEL)/%.cc
	$(TXX) $(TXFLAGS) $(GENDEP) -DC_FILE -o $@ $<
$(UNIT_BIN): $(TOBJS) | $(UNIT_LIB)
	$(TXX) -o $@ $^ -L$(OUTDIR) -lwheel $(TLFLAGS) -Wl,-rpath,".:$(OUTDIR)"

# 运行单元测试，生成代码覆盖率报告
$(UNIT_RAW): $(UNIT_BIN) $(UNIT_LIB)
	LLVM_PROFILE_FILE=$@ $<
$(UNIT_DAT): $(UNIT_RAW)
	$(LLVM_PROFDATA) merge -sparse $< -o $@
cov: $(UNIT_DAT)
	$(LLVM_COV) show $(UNIT_LIB) -compilation-dir . -instr-profile=$< \
		-format=html -show-regions --show-branches=count -o $(UNIT_COV)

-include $(OBJDEPS)
