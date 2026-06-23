# All-In-One build script for Wheel kernel and user apps

# build config
ARCH    ?= x86_64
DEBUG   ?= 1
OUTDIR  ?= build

# toolchain
KCC := $(TOOLCHAIN)clang
KLD := $(TOOLCHAIN)ld.lld
TCC := $(TOOLCHAIN)clang
TXX := $(TOOLCHAIN)clang++
GRUB_MKRESCUE := grub-mkrescue
LLVM_PROFDATA := llvm-profdata
LLVM_COV      := llvm-cov

# output files
KERNEL_ELF := $(OUTDIR)/wheel.elf
KERNEL_MAP := $(OUTDIR)/wheel.map
KUNIT_LIB  := $(OUTDIR)/libwheel.so
KUNIT_BIN  := $(OUTDIR)/kunit
KUNIT_RAW  := $(OUTDIR)/kunit.profraw
KUNIT_DAT  := $(OUTDIR)/kunit.profdata
KUNIT_COV  := $(OUTDIR)/kcoverage
ISO_DIR    := $(OUTDIR)/iso
BOOT_DIR   := $(ISO_DIR)/boot/grub
OUT_ISO    := $(OUTDIR)/cd.iso

# kernel source files
KERNEL_BASE := kernel
KERNEL_DIRS := arch_$(ARCH) core debug lib mem services
KERNEL_LD   := $(KERNEL_BASE)/arch_$(ARCH)/layout.ld
KSFILES := $(shell find $(KERNEL_DIRS:%=$(KERNEL_BASE)/%) -name "*.S")
KCFILES := $(shell find $(KERNEL_DIRS:%=$(KERNEL_BASE)/%) -name "*.c")
KXFILES := $(shell find $(KERNEL_DIRS:%=$(KERNEL_BASE)/%) -name "*.cc")

# kernel object files
KERNEL_OBJS := $(patsubst $(KERNEL_BASE)/%,$(OUTDIR)/k/%.ko,$(KSFILES) $(KCFILES))
LIBK_OBJS   := $(patsubst $(KERNEL_BASE)/%,$(OUTDIR)/k/%.to,$(KCFILES))
KUNIT_OBJS  := $(patsubst $(KERNEL_BASE)/%,$(OUTDIR)/k/%.to,$(KXFILES))

# user apps and outputs
USER_BASE := user
USER_APPS := test3 demo_float
USER_LD   := $(USER_BASE)/user.ld
LIBC_LIB  := $(OUTDIR)/libc.o
USER_ELFS := $(patsubst %,$(OUTDIR)/%.elf,$(USER_APPS))
USER_TAR  := $(OUTDIR)/users.tar
USER_DAT  := $(OUTDIR)/users.tar.dat

# user sources and objects
LIBC_FILES := $(shell find $(USER_BASE)/libc -name "*.c" -o -name "*.S")
USER_FILES := $(shell find $(USER_APPS:%=$(USER_BASE)/%) -name "*.c" -o -name "*.S")
LIBC_OBJS  := $(patsubst $(USER_BASE)/%,$(OUTDIR)/u/%.o,$(LIBC_FILES))
USER_OBJS  := $(patsubst $(USER_BASE)/%,$(OUTDIR)/u/%.o,$(USER_FILES))

# common compiler flags
GENDEP = -MT $@ -MMD -MP -MF $@.d

#-------------------------------------------------------------------------------
# 全局构建目标
#-------------------------------------------------------------------------------

.PHONY: kernel kunit kcov users iso clean

kernel: $(KERNEL_ELF)
kunit: $(KUNIT_BIN)
users: $(USER_ELFS)
iso: $(OUT_ISO)

clean:
	rm -rf $(OUTDIR)

ALLOBJS := $(KERNEL_OBJS) $(LIBK_OBJS) $(KUNIT_OBJS) $(LIBC_OBJS) $(USER_OBJS)
OBJDIRS := $(sort $(dir $(ALLOBJS)))
OBJDEPS := $(patsubst %,%.d,$(ALLOBJS))

# 创建目标文件所在目录
$(ALLOBJS) : | $(OBJDIRS)
$(OBJDIRS):
	mkdir -p $@

-include $(OBJDEPS)

#-------------------------------------------------------------------------------
# 内核编译链接选项
#-------------------------------------------------------------------------------

NOSTD  := -ffreestanding -fno-builtin -nostdlib
ASAN   := -fsanitize=address
GENCOV := -fprofile-instr-generate -fcoverage-mapping

KINC   := -I$(KERNEL_BASE)/api $(KERNEL_DIRS:%=-I$(KERNEL_BASE)/%)

# 内核编译选项，C & ASM
KCFLAGS := -c -std=c11 -target $(ARCH)-none-elf $(KINC) $(NOSTD)
KCFLAGS += -ffunction-sections -fdata-sections -fvisibility=hidden
KCFLAGS += -Wall -Wextra -Wshadow -Werror=implicit -fstack-usage
ifeq ($(DEBUG),1)
	KCFLAGS += -DDEBUG -g -fno-omit-frame-pointer -fstack-protector-strong
else
	KCFLAGS += -DNDEBUG -O2
endif

# 内核链接选项
KLFLAGS := -T $(KERNEL_LD) -Map=$(KERNEL_MAP)
KLFLAGS += -nostdlib --gc-sections --no-warnings

# 内核库编译选项，生成单元测试用到的动态库
LCFLAGS := -c -std=c11 -g -fPIC -DDEBUG -DUNIT_TEST
LCFLAGS += $(KINC) $(NOSTD) $(GENCOV) $(ASAN)

# 内核库链接选项
LLFLAGS := -shared $(GENCOV) $(ASAN)
LLFLAGS += -Wl,--allow-shlib-undefined -fsanitize=address

# 单元测试代码的编译选项，C++
TXFLAGS := -c -std=c++17 -g -DDEBUG -DUNIT_TEST $(KINC)
TXFLAGS += -isystem -pthread $(ASAN)

# 单元测试链接选项
TLFLAGS := -lgtest -lgtest_main $(ASAN)

include $(KERNEL_BASE)/arch_$(ARCH)/config.mk

#-------------------------------------------------------------------------------
# 用户态程序编译选项
#-------------------------------------------------------------------------------

USER_CFLAGS := -target $(ARCH)-none-elf -ffreestanding -nostdlib
USER_CFLAGS += -fno-stack-protector #-mgeneral-regs-only
USER_CFLAGS += -I$(USER_BASE)/libc
ifeq ($(DEBUG),1)
	USER_CFLAGS += -g
else
	USER_CFLAGS += -O2
endif

#-------------------------------------------------------------------------------
# 内核编译规则
#-------------------------------------------------------------------------------

# 编译内核
$(OUTDIR)/k/%.S.ko: $(KERNEL_BASE)/%.S
	$(KCC) $(KCFLAGS) $(GENDEP) -DS_FILE -o $@ $<
$(OUTDIR)/k/%.c.ko: $(KERNEL_BASE)/%.c
	$(KCC) $(KCFLAGS) $(GENDEP) -DC_FILE -o $@ $<
$(KERNEL_ELF): $(KERNEL_OBJS) $(USER_DAT)
	$(KLD) $(KLFLAGS) -o $@ $^


# 内核库，单元测试用，只包括 C 代码，使用默认工具链
$(OUTDIR)/k/%.c.to: $(KERNEL_BASE)/%.c
	$(TCC) $(LCFLAGS) $(GENDEP) -DC_FILE -o $@ $<
$(KUNIT_LIB): $(LIBK_OBJS)
	$(TXX) $(LLFLAGS) -o $@ $^

# 编译单元测试程序，使用默认工具链
$(OUTDIR)/k/%.cc.to: $(KERNEL_BASE)/%.cc
	$(TXX) $(TXFLAGS) $(GENDEP) -DC_FILE -o $@ $<
$(KUNIT_BIN): $(KUNIT_OBJS) | $(KUNIT_LIB)
	$(TXX) -o $@ $^ -L$(OUTDIR) -lwheel $(TLFLAGS) -Wl,-rpath,".:$(OUTDIR)"

# 运行单元测试，生成代码覆盖率报告
$(KUNIT_RAW): $(KUNIT_BIN) $(KUNIT_LIB)
	LLVM_PROFILE_FILE=$@ $<
$(KUNIT_DAT): $(KUNIT_RAW)
	$(LLVM_PROFDATA) merge -sparse $< -o $@
kcov: $(KUNIT_DAT)
	$(LLVM_COV) show $(KUNIT_LIB) -compilation-dir . -instr-profile=$< \
		-format=html -show-regions --show-branches=count -o $(KUNIT_COV)


#-------------------------------------------------------------------------------
# 用户态程序编译规则
#-------------------------------------------------------------------------------

$(OUTDIR)/u/%.c.o: $(USER_BASE)/%.c
	$(KCC) -c -DC_FILE $(USER_CFLAGS) $(GENDEP) -o $@ $<

$(OUTDIR)/u/%.S.o: $(USER_BASE)/%.S
	$(KCC) -c -DS_FILE $(USER_CFLAGS) $(GENDEP) -o $@ $<

$(LIBC_LIB): $(LIBC_OBJS)
	$(KLD) -r -o $@ $^

# 链接用户态程序，需要按目录筛选属于该程序的目标文件
define make_user_elf
$(1)_OBJS := $$(filter $(OUTDIR)/u/$(1)/%,$(USER_OBJS))
$(OUTDIR)/$(1).elf: $$($(1)_OBJS) $$(LIBC_LIB) | $(USER_LD)
	$$(KLD) -T $(USER_LD) -o $$@ $$^
endef
$(foreach d,$(USER_APPS),$(eval $(call make_user_elf,$(d))))

# cd到输出目录再打包，这样tar里面不含路径
# $(<D) 表示目标所在目录
$(USER_TAR): $(USER_ELFS)
	cd $(<D) && tar cf $(abspath $@) $(notdir $^)

# 需要cd到文件所在目录，这样符号名不含路径
# $(<F) 表示目标文件名（不含路径）
$(USER_DAT): $(USER_TAR)
	cd $(<D) && objcopy -I binary $(OBJCOPY_ARCH) $(<F) $(abspath $@)

#-------------------------------------------------------------------------------
# 制作启动镜像
#-------------------------------------------------------------------------------

# 各行命令使用同一个 shell（gnu-make only）
# 否则写入多行字符串会被拆成多个命令
.ONESHELL:

# kernel 输出 wheel.elf，user 输出 initrd.tar
$(OUT_ISO): $(KERNEL_ELF)
	@rm -rf $(ISO_DIR)
	@mkdir -p $(BOOT_DIR)
	@cat << EOF > $(BOOT_DIR)/grub.cfg
	set default=0
	GRUB_GFXMODE=auto
	menuentry "wheel (multiboot 2, graphical)" {
	    multiboot2 /$(notdir $<)
	}
	menuentry "wheel (multiboot 1)" {
	    multiboot /$(notdir $<)
	}
	EOF
	@cp $< $(ISO_DIR)/
	$(GRUB_MKRESCUE) -o $@ $(ISO_DIR)
