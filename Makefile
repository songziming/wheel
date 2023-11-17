# build script for wheel os

DEBUG   ?= 1
ARCH    := x86_64

# 编译内核用的编译器和链接器
KCC     := clang --target=$(ARCH)-pc-none-elf
KLD     := ld.lld
TCC     := clang
TXX     := clang++

OUT_DIR := build
ISO_DIR := $(OUT_DIR)/iso
COV_DIR := $(OUT_DIR)/cov

OUT_ELF := $(OUT_DIR)/wheel.elf
OUT_MAP := $(OUT_DIR)/wheel.map
OUT_TEST := $(OUT_DIR)/test



define get_subdirs
    $(foreach d,$(1),$(wildcard $(d)/$(2)))
endef

define get_sources
    $(foreach d,$(1),$(shell find $(d) -name "*.S" -o -name "*.c" -o -name "*.cc"))
endef

define all_files
    $(call get_sources,$(call get_subdirs,$(1),$(2)))
endef



KDIRS := kernel_1/arch_$(ARCH)
KDIRS += $(filter-out $(wildcard kernel_1/arch_*),$(wildcard kernel_1/*))
KINCS := $(patsubst %,%/headers,$(KDIRS)) kernel_1
KOBJS := $(patsubst %,$(OUT_DIR)/objs/%.ko,$(call all_files,$(KDIRS),sources))

TSRCS := $(call all_files,$(KDIRS),tests) tools/kernel_test/test.c
TOBJS := $(patsubst %,$(OUT_DIR)/objs/%.to,$(TSRCS))

KCFLAGS := -std=c11 $(KINCS:%=-I%) -Wall -Wextra -Wshadow -Werror=implicit
KCFLAGS += -ffreestanding -fno-builtin -flto -ffunction-sections -fdata-sections
ifeq ($(DEBUG),1)
    KCFLAGS += -g -DDEBUG
else
    KCFLAGS += -O2 -DNDEBUG
endif

KLAYOUT := kernel_1/arch_$(ARCH)/layout.ld
KLFLAGS := -nostdlib --gc-sections -Map=$(OUT_MAP) -T $(KLAYOUT) --no-warnings

TCFLAGS := -g -DUNIT_TEST $(KINCS:%=-I%) -I tools/kernel_test
TCFLAGS += -fsanitize=address -fprofile-arcs -ftest-coverage

GCOV_FULL := $(OUT_DIR)/gcov.full
GCOV_LEAN := $(OUT_DIR)/gcov.lean




ALL_OBJS := $(KOBJS) $(TOBJS)
ALL_DEPS := $(patsubst %,%.d,$(ALL_OBJS))
ALL_DIRS := $(sort $(dir $(ALL_DEPS)) $(ISO_DIR)/boot/grub)

DEPGEN = -MT $@ -MMD -MP -MF $@.d

include kernel_1/arch_$(ARCH)/option.mk



.PHONY: kernel test cov clean

kernel: $(OUT_ELF)

test: $(OUT_TEST)

cov: $(COV_DIR)

clean:
	@ rm -rf $(OUT_DIR)



$(ALL_OBJS): | $(ALL_DIRS)

$(ALL_DIRS):
	@ mkdir -p $@



# 编译内核镜像
$(OUT_DIR)/objs/%.S.ko: %.S
	$(KCC) -c -DS_FILE $(KCFLAGS) $(DEPGEN) -o $@ $<
$(OUT_DIR)/objs/%.c.ko: %.c
	$(KCC) -c -DC_FILE $(KCFLAGS) $(DEPGEN) -o $@ $<
$(OUT_ELF): $(KOBJS) | $(KLAYOUT)
	$(KLD) $(KLFLAGS) -o $@ $^



# 编译单元测试
$(OUT_DIR)/objs/%.c.to: %.c
	$(TCC) -c -DC_FILE -std=c11 $(TCFLAGS) $(DEPGEN) -o $@ $<
$(OUT_DIR)/objs/%.cc.to: %.cc
	$(TXX) -c -DC_FILE -std=c++14 $(TCFLAGS) $(DEPGEN) -o $@ $<
$(OUT_TEST): $(TOBJS)
	$(TXX) $(TCFLAGS) -o $@ $^ -lm -pthread



# 生成代码覆盖率报告
FORCE: ;
$(GCOV_FULL): FORCE
	lcov -d $(OUT_DIR) -b . -c -o $@
$(GCOV_LEAN): $(GCOV_FULL)
	lcov -r $< -o $@ '*/headers/*' '/usr/*'
$(COV_DIR): $(GCOV_LEAN)
	genhtml -o $@ $<



-include $(ALL_DEPS)
