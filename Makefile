# build wheel kernel and unit test




# build settings
ARCH  ?= x86_64
DEBUG ?= 1

UBSAN ?= $(DEBUG)
KASAN ?= 0 #$(DEBUG)
KTEST ?= $(DEBUG)
KCOV  ?= 1

# toolchain
KCC := $(TOOLCHAIN)clang
KLD := $(TOOLCHAIN)ld.lld
TCC := $(TOOLCHAIN)clang
TXX := $(TOOLCHAIN)clang++

# host-dependent tools
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
	GRUB_MKRESCUE=i686-elf-grub-mkrescue
	LLVM_PROFDATA=xcrun llvm-profdata
	LLVM_COV=xcrun llvm-cov
else
	GRUB_MKRESCUE=grub-mkrescue
	LLVM_PROFDATA=llvm-profdata
	LLVM_COV=llvm-cov
endif

# output dir and files
OUT_DIR ?= build
ISO_DIR := $(OUT_DIR)/iso
OUT_ELF := $(OUT_DIR)/wheel.elf
OUT_MAP := $(OUT_DIR)/wheel.map
OUT_ISO := $(OUT_DIR)/cd.iso
OUT_IMG := $(OUT_DIR)/hd.img

# unit test output
UNIT_LIB := $(OUT_DIR)/libwheel.so
UNIT_BIN := $(OUT_DIR)/unit
UNIT_RAW := $(OUT_DIR)/unit.profraw
UNIT_DAT := $(OUT_DIR)/unit.profdata
UNIT_COV := $(OUT_DIR)/coverage

# source files and objects
KERNEL := kernel
KDIRS  := arch_$(ARCH) core lib mem
AFILES := $(shell find $(KDIRS:%=$(KERNEL)/%) -name "*.S")
CFILES := $(shell find $(KDIRS:%=$(KERNEL)/%) -name "*.c")
XFILES := $(shell find $(KDIRS:%=$(KERNEL)/%) -name "*.cc")

KOBJS := $(patsubst $(KERNEL)/%,$(OUT_DIR)/%.ko,$(AFILES) $(CFILES))
LOBJS := $(patsubst $(KERNEL)/%,$(OUT_DIR)/%.to,$(CFILES))
TOBJS := $(patsubst $(KERNEL)/%,$(OUT_DIR)/%.to,$(XFILES))

ALLOBJS := $(KOBJS) $(LOBJS) $(TOBJS)
OBJDIRS := $(sort $(dir $(ALLOBJS)))
OBJDEPS := $(patsubst %,%.d,$(ALLOBJS))


#-------------------------------------------------------------------------------
# 编译链接选项
#-------------------------------------------------------------------------------

KINC   := $(KDIRS:%=-I$(KERNEL)/%)
NOSTD  := -ffreestanding -fno-builtin -nostdlib
ASAN   := -fsanitize=address
GENCOV := -fprofile-instr-generate -fcoverage-mapping -fdebug-compilation-dir $(KERNEL)
GENDEP  = -MT $@ -MMD -MP -MF $@.d

# 内核编译选项，C & ASM
KCFLAGS := -c -std=c11 -target $(ARCH)-none-elf $(KINC) $(NOSTD)
KCFLAGS += -ffunction-sections -fdata-sections -fstack-protector-strong
KCFLAGS += -Wall -Wextra -Wshadow -Werror=implicit
ifeq ($(DEBUG),1)
	KCFLAGS += -DDEBUG -g -fno-omit-frame-pointer
else
	KCFLAGS += -DNDEBUG -O2
endif

# 内核链接选项
KLFLAGS := -T $(KERNEL)/arch_$(ARCH)/layout.ld -Map=$(OUT_MAP)
KLFLAGS += -nostdlib --gc-sections --no-warnings

# 内核库编译选项，生成单元测试用到的动态库，C & ASM
LCFLAGS := -c -std=c11 -g -fPIC -DUNIT_TEST $(KINC) $(NOSTD) $(GENCOV) $(ASAN)

# 内核库链接选项
LLFLAGS := -shared $(GENCOV) $(ASAN)

# 允许内核库中出现未定义的符号
ifeq ($(UNAME_S),Darwin)
#  macos 不支持 asan
	LLFLAGS += -Wl,-undefined,dynamic_lookup,-flat_namespace
else ifeq ($(UNAME_S),Linux)
	LLFLAGS += -Wl,--allow-shlib-undefined -fsanitize=address
endif

# 单元测试代码的编译选项，C++
TXFLAGS := -c -std=c++17 -g -DUNIT_TEST $(KINC) $(ASAN)
TXFLAGS += $(shell pkg-config --cflags gtest)

# 单元测试链接选项
TLFLAGS := $(shell pkg-config --libs gtest gtest_main) $(ASAN)

include $(KERNEL)/arch_$(ARCH)/config.mk


#-------------------------------------------------------------------------------
# 构建规则
#-------------------------------------------------------------------------------

.PHONY: elf iso unit cov clean

elf: $(OUT_ELF)
iso: $(OUT_ISO)
unit: $(UNIT_BIN)

clean:
	rm -rf $(OUT_DIR)

# 创建目标文件所在目录
$(ALLOBJS) : | $(OBJDIRS)
$(OBJDIRS):
	mkdir -p $@

# 编译内核
$(OUT_DIR)/%.S.ko: $(KERNEL)/%.S
	$(KCC) $(KCFLAGS) $(GENDEP) -DS_FILE -o $@ $<
$(OUT_DIR)/%.c.ko: $(KERNEL)/%.c
	$(KCC) $(KCFLAGS) $(GENDEP) -DC_FILE -o $@ $<
$(OUT_ELF): $(KOBJS)
	$(KLD) $(KLFLAGS) -o $@ $^

# 内核库，单元测试用，只包括 C 代码，使用默认工具链
$(OUT_DIR)/%.c.to: $(KERNEL)/%.c
	$(TCC) $(LCFLAGS) $(GENDEP) -DC_FILE -o $@ $<
$(UNIT_LIB): $(LOBJS)
	$(TXX) $(LLFLAGS) -o $@ $^

# 编译单元测试程序，使用默认工具链
# 链接单元测试程序时，指定 rpath，便于在运行目录下寻找 libwheel.so
$(OUT_DIR)/%.cc.to: $(KERNEL)/%.cc
	$(TXX) $(TXFLAGS) $(GENDEP) -DC_FILE -o $@ $<
$(UNIT_BIN): $(TOBJS) | $(UNIT_LIB)
	$(TXX) -o $@ $^ -L$(OUT_DIR) -lwheel $(TLFLAGS) -Wl,-rpath,".:$(OUT_DIR)"

# 运行单元测试，生成代码覆盖率报告
$(UNIT_RAW): $(UNIT_BIN) $(UNIT_LIB)
# 	LLVM_PROFILE_FILE=$@ LD_LIBRARY_PATH=$(OUT_DIR) $<
	LLVM_PROFILE_FILE=$@ $<
$(UNIT_DAT): $(UNIT_RAW)
	$(LLVM_PROFDATA) merge -sparse $< -o $@
cov: $(UNIT_DAT)
	$(LLVM_COV) show $(UNIT_LIB) -compilation-dir . -instr-profile=$< \
		-format=html -show-regions --show-branches=count -o $(UNIT_COV)

-include $(OBJDEPS)


#-------------------------------------------------------------------------------
# 生成引导介质
#-------------------------------------------------------------------------------

# 各行命令使用同一个 shell（gnu-make only）
# 否则写入多行字符串会被拆成多个命令
.ONESHELL:

$(OUT_ISO): $(OUT_ELF)
	@rm -rf $(ISO_DIR)
	@mkdir -p $(ISO_DIR)/boot/grub
	@cat << EOF > $(ISO_DIR)/boot/grub/grub.cfg
	set default=0
	GRUB_GFXMODE=auto
	menuentry "wheel (multiboot 2, graphical)" {
	    multiboot2 /wheel.elf
	}
	menuentry "wheel (multiboot 1)" {
	    multiboot /wheel.elf
	}
	EOF
	@cp $< $(ISO_DIR)/wheel.elf
	@$(GRUB_MKRESCUE) -o $@ $(ISO_DIR)
