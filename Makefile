# build wheel kernel and unit test


UNAME_S := $(shell uname -s)

ifeq ($(UNAME_S),Darwin)
    TOOLCHAIN=$(shell brew --prefix llvm)/bin/
    GRUB_MKRESCUE=i686-elf-grub-mkrescue
else
    GRUB_MKRESCUE=grub-mkrescue
endif



# build settings
ARCH  ?= x86_64
DEBUG ?= 1

UBSAN ?= $(DEBUG)
KASAN ?= 0 #$(DEBUG)
KTEST ?= $(DEBUG)
KCOV  ?= 1

# toolchain
KCC := $(TOOLCHAIN)clang
KXX := $(TOOLCHAIN)clang++
KLD := ld.lld

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
KDIRS  := core #lib mem
AFILES := $(shell find $(KERNEL)/arch_$(ARCH) -name "*.S" -o -name "*.c")
KFILES := $(shell find $(KDIRS:%=$(KERNEL)/%) -name "*.c")
TFILES := $(shell find $(KDIRS:%=$(KERNEL)/%) -name "*.cc")

KOBJS := $(patsubst $(KERNEL)/%,$(OUT_DIR)/%.ko,$(AFILES) $(KFILES))
LOBJS := $(patsubst $(KERNEL)/%,$(OUT_DIR)/%.to,$(AFILES) $(KFILES))
TOBJS := $(patsubst $(KERNEL)/%,$(OUT_DIR)/%.to,$(TFILES))

ALLOBJS := $(KOBJS) $(LOBJS) $(TOBJS)
OBJDIRS := $(sort $(dir $(ALLOBJS)))
OBJDEPS := $(patsubst %,%.d,$(ALLOBJS))


#-------------------------------------------------------------------------------
# 编译链接选项
#-------------------------------------------------------------------------------

NOSTD := -ffreestanding -fno-builtin -nostdlib
KINC := $(KDIRS:%=-I$(KERNEL)/%) -I$(KERNEL)/arch_$(ARCH)

# 内核编译选项，C & asm
KCFLAGS := -c -std=c11 -target $(ARCH)-none-elf $(KINC)
KCFLAGS += -ffunction-sections -fdata-sections -fstack-protector-strong
KCFLAGS += -Wall -Wextra -Wshadow -Werror=implicit

# 内核链接选项
KLFLAGS := -T $(KERNEL)/arch_$(ARCH)/layout.ld -Map=$(OUT_MAP)
KLFLAGS += -nostdlib --gc-sections --no-warnings

# 单元测试编译选项，C & C++
TCFLAGS := -c -g -DUNIT_TEST $(KINC)
TCFLAGS += $(shell pkg-config --cflags gtest)

ifeq ($(UNAME_S),Darwin)
ALLOWUNDEF := -Wl,-undefined,dynamic_lookup,-flat_namespace
else ifeq ($(UNAME_S),Linux)
TCFLAGS += -fsanitize=address
ALLOWUNDEF := -Wl,--allow-shlib-undefined -fsanitize=address
endif

# 单元测试链接选项
TLFLAGS := $(shell pkg-config --libs gtest gtest_main)

GENDEP = -MT $@ -MMD -MP -MF $@.d
GENCOV := -fprofile-instr-generate -fcoverage-mapping

ifeq ($(DEBUG),1)
	KCFLAGS += -DDEBUG -g -fno-omit-frame-pointer
else
	KCFLAGS += -DNDEBUG -O2
endif

include $(KERNEL)/arch_$(ARCH)/config.mk


#-------------------------------------------------------------------------------
# 构建规则
#-------------------------------------------------------------------------------

.PHONY: elf iso unit cov clean loc

elf: $(OUT_ELF)
iso: $(OUT_ISO)
unit: $(UNIT_BIN)

clean:
	rm -rf $(OUT_DIR)

loc:
	find $(KERNEL) -name "*.h" -o -name "*.c" -o -name "*.S" | xargs wc -l

# 创建目标文件所在目录
$(ALLOBJS) : | $(OBJDIRS)
$(OBJDIRS):
	mkdir -p $@

# 编译内核
$(OUT_DIR)/%.S.ko: $(KERNEL)/%.S
	$(KCC) $(KCFLAGS) $(KINC) $(NOSTD) $(GENDEP) -DS_FILE -o $@ $<
$(OUT_DIR)/%.c.ko: $(KERNEL)/%.c
	$(KCC) $(KCFLAGS) $(KINC) $(NOSTD) $(GENDEP) -DC_FILE -o $@ $<
$(OUT_ELF): $(KOBJS)
	$(KLD) $(KLFLAGS) -o $@ $^

# 编译单元测试程序
$(OUT_DIR)/%.S.to: $(KERNEL)/%.S
	$(KCC) -std=c11 -fPIC $(TCFLAGS) $(NOSTD) $(GENCOV) $(GENDEP) -DS_FILE -o $@ $<
$(OUT_DIR)/%.c.to: $(KERNEL)/%.c
	$(KCC) -std=c11 -fPIC $(TCFLAGS) $(NOSTD) $(GENCOV) $(GENDEP) -DC_FILE -o $@ $<
$(OUT_DIR)/%.cc.to: $(KERNEL)/%.cc
	$(KXX) -std=c++17 $(TCFLAGS) $(GENDEP) -DC_FILE -o $@ $<
$(UNIT_LIB): $(LOBJS)
	$(KCC) $(GENCOV) $(ALLOWUNDEF) -shared -o $@ $^
$(UNIT_BIN): $(TOBJS) | $(UNIT_LIB)
	$(KXX)  $(ALLOWUNDEF) -o $@ $^ -L$(OUT_DIR) -lwheel $(TLFLAGS)

# 运行单元测试，生成代码覆盖率报告
$(UNIT_RAW): $(UNIT_BIN) $(UNIT_LIB)
	LLVM_PROFILE_FILE=$@ LD_LIBRARY_PATH=$(OUT_DIR) $<
$(UNIT_DAT): $(UNIT_RAW)
	$(TOOLCHAIN)llvm-profdata merge -sparse $< -o $@
cov: $(UNIT_DAT)
	$(TOOLCHAIN)llvm-cov show $(UNIT_LIB) -instr-profile=$< -format=html -o $(UNIT_COV)

-include $(OBJDEPS)


#-------------------------------------------------------------------------------
# 生成引导介质
#-------------------------------------------------------------------------------

.ONESHELL:

$(OUT_ISO): $(OUT_ELF)
	@rm -rf $(ISO_DIR)
	@mkdir -p $(ISO_DIR)/boot/grub
	@cat << EOF > $(ISO_DIR)/boot/grub/grub.cfg
	set default=0
	GRUB_GFXMODE=auto
	menuentry "wheel (multiboot 2, graphical)" {
	    multiboot2 /wheel.elf
	    # module2 /init.text option to init text
	}
	menuentry "wheel (multiboot 1)" {
	    multiboot /wheel.elf
	}
	EOF
	@cp $< $(ISO_DIR)/wheel.elf
	@$(GRUB_MKRESCUE) -o $@ $(ISO_DIR)
