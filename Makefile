# wheel os 唯一的编译脚本
# 编译内核的所有模块，生成可引导内核镜像文件


#-------------------------------------------------------------------------------
# 配置参数
#-------------------------------------------------------------------------------

# 工具链
DEBUG   ?= 1
ARCH    := x86_64

# 编译内核用的编译器和链接器
KCC     := clang --target=$(ARCH)-pc-none-elf
KLD     := ld.lld

# 编译单元测试用的编译器（host）
TCC     := gcc
TXX     := g++

# 输出目录
OUT_DIR := build
ISO_DIR := $(OUT_DIR)/iso
COV_DIR := $(OUT_DIR)/coverage

# 输出文件
OUT_ELF := $(OUT_DIR)/wheel.elf
OUT_MAP := $(OUT_DIR)/wheel.map
OUT_SYM := $(OUT_DIR)/wheel.sym
OUT_ISO := $(OUT_DIR)/wheel.iso
OUT_IMG := $(OUT_DIR)/disk.img
OUT_TST := $(OUT_DIR)/test

# 内核各模块代码，其中 arch 要选择对应目标平台
# KERN_SUBDIRS := kernel/arch_$(ARCH)/sources
# KERN_SUBDIRS += $(filter-out $(wildcard kernel/arch_*/sources),$(wildcard kernel/*/sources))
# KERN_INCDIRS := $(patsubst %/sources,%/headers,$(KERN_SUBDIRS)) kernel/core
# KERN_SOURCES := $(foreach d,$(KERN_SUBDIRS),$(shell find $(d) -name "*.S" -o -name "*.c"))

KERN_SUBDIRS := arch_$(ARCH) kernel
KERN_SOURCES := $(foreach d,$(KERN_SUBDIRS),$(shell find $(d)/sources -name "*.S" -o -name "*.c"))
KERN_OBJECTS := $(patsubst %,$(OUT_DIR)/%.ko,$(KERN_SOURCES))

# 内核编译选项，开启链接时优化、section 引用计数
KERN_CFLAGS  := -std=c11 $(KERN_SUBDIRS:%=-I%/headers)
KERN_CFLAGS  += -Wall -Wextra -Wshadow -Werror=implicit
KERN_CFLAGS  += -ffreestanding -fno-builtin -flto -ffunction-sections -fdata-sections
ifeq ($(DEBUG),1)
    KERN_CFLAGS += -g -DDEBUG
else
    KERN_CFLAGS += -O2 -DNDEBUG
endif

# 内核链接脚本（平台相关）
KERN_LAYOUT  := kernel/arch_$(ARCH)/layout.ld

# 内核链接选项
# 需要加上 no-warnings，否则 lld 会报 section type mismatch，但链接会正常执行
# 这是因为我们把自定义的 section 放在了 noload section 里面
# 参见：https://github.com/ClangBuiltLinux/linux/issues/1597
KERN_LDFLAGS := -nostdlib --gc-sections -Map=$(OUT_MAP) -T $(KERN_LAYOUT) --no-warnings

# 单元测试工具（host 环境下测试 kernel 代码）
TEST_SUBDIRS := $(wildcard kernel/*/tests)
TEST_SOURCES := $(foreach d,$(TEST_SUBDIRS),$(shell find $(d) -name "*.c" -o -name "*.cc"))
TEST_SOURCES += tools/kernel_test/test.c
TEST_OBJECTS := $(patsubst %,$(OUT_DIR)/%.to,$(TEST_SOURCES))

TEST_CFLAGS  := -g -DUNIT_TEST $(KERN_INCDIRS:%=-I%) -I tools/kernel_test
TEST_CFLAGS  += -fsanitize=address -fprofile-arcs -ftest-coverage


# 覆盖率数据文件
GCOV_FULL := $(OUT_DIR)/gcov.full
GCOV_LEAN := $(OUT_DIR)/gcov.lean

# 依赖关系文件和临时目录
ALL_OBJS := $(KERN_OBJECTS) $(TEST_OBJECTS)
OBJ_DEPS := $(patsubst %,%.d,$(ALL_OBJS))
OBJ_DIRS := $(sort $(dir $(OBJ_DEPS)) $(ISO_DIR)/boot/grub)

# 生成依赖文件用的编译选项
DEP_FLAGS = -MT $@ -MMD -MP -MF $@.d

# 平台相关的编译规则
include kernel/arch_$(ARCH)/option.mk



#===============================================================================

.PHONY: kernel iso img test cov clean

kernel: $(OUT_ELF) $(OUT_SYM)

iso: $(OUT_ISO)

img: $(OUT_IMG)

test: $(OUT_TST)

cov: $(COV_DIR)

clean:
	@ rm -rf $(ISO_DIR)
	@ rm -rf $(COV_DIR)
	@ rm -rf $(OUT_DIR)



# 创建目标文件所在的目录

$(ALL_OBJS): | $(OBJ_DIRS)

$(OBJ_DIRS):
	mkdir -p $@



# 编译内核

$(OUT_DIR)/%.S.ko: %.S
	$(KCC) -c -DS_FILE $(KERN_CFLAGS) $(DEP_FLAGS) -o $@ $<

$(OUT_DIR)/%.c.ko: %.c
	$(KCC) -c -DC_FILE $(KERN_CFLAGS) $(DEP_FLAGS) -o $@ $<

$(OUT_ELF): $(KERN_OBJECTS) | $(KERN_LAYOUT)
	$(KLD) $(KERN_LDFLAGS) -o $@ $^

$(OUT_SYM): $(OUT_ELF)
	nm $< | awk '{ print $1" "$3 }' > $@


# 创建启动介质

$(OUT_ISO): $(OUT_ELF) tools/grub.cfg
	@ cp $(OUT_ELF) $(ISO_DIR)/wheel.elf
	@ cp tools/grub.cfg $(ISO_DIR)/boot/grub/grub.cfg
	@ grub-mkrescue -d tools/grub-i386-pc -o $@ $(ISO_DIR)

$(OUT_IMG): $(OUT_ELF) tools/grub.cfg
	@ tools/diskimg_create.sh $@
	@ tools/diskimg_update.sh $(OUT_ELF) $@ wheel.elf
	@ tools/diskimg_update.sh tools/grub.cfg $@ boot/grub/grub.cfg



# 编译单元测试工具

$(OUT_DIR)/%.c.to: %.c
	$(TCC) -c -DC_FILE -std=c11 $(TEST_CFLAGS) $(DEP_FLAGS) -o $@ $<

$(OUT_DIR)/%.cc.to: %.cc
	$(TXX) -c -DC_FILE -std=c++14 $(TEST_CFLAGS) $(DEP_FLAGS) -o $@ $<

$(OUT_TST): $(TEST_OBJECTS)
	$(TXX) $(TEST_CFLAGS) -o $@ $^ -lm -pthread



# 代码覆盖率报告

FORCE: ;

$(GCOV_FULL): FORCE
	lcov -d $(OUT_DIR) -b . -c -o $@

$(GCOV_LEAN): $(GCOV_FULL)
	lcov -r $< -o $@ '*/headers/*' '/usr/*'

$(COV_DIR): $(GCOV_LEAN)
	genhtml -o $@ $<



# 包含所有的依赖关系文件，必须放在最后
-include $(OBJ_DEPS)
