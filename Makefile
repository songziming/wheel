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
OUT_ISO := $(OUT_DIR)/wheel.iso
OUT_IMG := $(OUT_DIR)/disc.img
OUT_TEST := $(OUT_DIR)/test


define filterout # pattern list
    $(foreach v,$(2),$(if $(findstring $(1),$(v)),,$(v)))
endef

define all_nonarch_dirs
    $(call filterout,arch,$(patsubst %/.,%,$(wildcard kernel/*/.)))
endef

define get_subdirs # dirs subdir-pattern
    $(foreach d,$(1),$(wildcard $(d)/$(2)))
endef

define get_sources # dirs
    $(foreach d,$(1),$(shell find $(d) -name "*.S" -o -name "*.c" -o -name "*.cc"))
endef

define all_files # dirs mid_dir
    $(call get_sources,$(call get_subdirs,$(1),$(2)))
endef



KDIRS := kernel/arch_$(ARCH) $(call all_nonarch_dirs)
KINCS := kernel kernel/arch_$(ARCH) $(patsubst %,%/headers,$(KDIRS))
KOBJS := $(patsubst %,$(OUT_DIR)/objs/%.ko,$(call all_files,$(KDIRS),sources))

TSRCS := $(call all_files,$(KDIRS),tests) host_tools/kernel_test/test.c
TOBJS := $(patsubst %,$(OUT_DIR)/objs/%.to,$(TSRCS))

KCFLAGS := -std=c11 $(KINCS:%=-I%) -Wall -Wextra -Wshadow -Werror=implicit
KCFLAGS += -ffreestanding -fno-builtin -flto -ffunction-sections -fdata-sections
ifeq ($(DEBUG),1)
    KCFLAGS += -g -DDEBUG -fstack-protector
    KCFLAGS += -fsanitize=undefined -fno-sanitize=function
    # KCFLAGS += -fsanitize=kernel-address -fno-omit-frame-pointer
else
    KCFLAGS += -O2 -DNDEBUG
endif

KLAYOUT := kernel/arch_$(ARCH)/layout.ld
KLFLAGS := -nostdlib --gc-sections -Map=$(OUT_MAP) -T $(KLAYOUT)  --no-warnings

TCFLAGS := -g -DUNIT_TEST -Itools/kernel_test
TCFLAGS += -fsanitize=address -fprofile-instr-generate -fcoverage-mapping
COV_RAW := $(OUT_DIR)/test.profraw
COV_DAT := $(OUT_DIR)/test.profdata



DEP_GEN = -MT $@ -MMD -MP -MF $@.d

ALL_OBJS := $(KOBJS) $(TOBJS)
ALL_DEPS := $(patsubst %,%.d,$(ALL_OBJS))
ALL_DIRS := $(sort $(dir $(ALL_DEPS)) $(ISO_DIR)/boot/grub)

include kernel/arch_$(ARCH)/option.mk



.PHONY: kernel iso img test cov clean

kernel: $(OUT_ELF)
iso: $(OUT_ISO)
img: $(OUT_IMG)
test: $(OUT_TEST)
cov: $(COV_DIR)

clean:
	@ rm -rf $(OUT_DIR)



$(ALL_OBJS): | $(ALL_DIRS)

$(ALL_DIRS):
	@ mkdir -p $@



# 编译内核镜像
$(OUT_DIR)/objs/%.S.ko: %.S
	$(KCC) -c -DS_FILE $(KCFLAGS) $(DEP_GEN) -o $@ $<
$(OUT_DIR)/objs/%.c.ko: %.c
	$(KCC) -c -DC_FILE $(KCFLAGS) $(DEP_GEN) -o $@ $<
$(OUT_ELF): $(KOBJS) | $(KLAYOUT)
	$(KLD) $(KLFLAGS) -o $@ $^



# 创建引导介质
$(OUT_ISO): $(OUT_ELF) host_tools/grub.cfg
	@ cp $(OUT_ELF) $(ISO_DIR)/wheel.elf
	@ cp host_tools/grub.cfg $(ISO_DIR)/boot/grub/grub.cfg
	@ grub-mkrescue -d host_tools/grub-i386-pc -o $@ $(ISO_DIR)
$(OUT_IMG): $(OUT_ELF) host_tools/grub.cfg
	@ host_tools/diskimg_create.sh $@
	@ host_tools/diskimg_update.sh $(OUT_ELF) $@ wheel.elf
	@ host_tools/diskimg_update.sh host_tools/grub.cfg $@ boot/grub/grub.cfg



# 编译单元测试（C++ 源文件不能引用内核头文件，避免头文件冲突）
$(OUT_DIR)/objs/%.c.to: %.c
	$(TCC) -c -DC_FILE -std=c11 $(KINCS:%=-I%) $(TCFLAGS) $(DEP_GEN) -o $@ $<
$(OUT_DIR)/objs/%.cc.to: %.cc
	$(TXX) -c -DC_FILE -std=c++14 -fpermissive $(TCFLAGS) $(DEP_GEN) -o $@ $<
$(OUT_TEST): $(TOBJS)
	$(TXX) $(TCFLAGS) -o $@ $^ -lm -pthread



# 生成代码覆盖率报告
$(COV_RAW): $(OUT_TEST)
	LLVM_PROFILE_FILE=$@ $<
$(COV_DAT): $(COV_RAW)
	llvm-profdata merge -sparse $< -o $@
$(COV_DIR): $(COV_DAT) | $(OUT_TEST)
	llvm-cov show $(OUT_TEST) -instr-profile=$< -format=html -o $@



# # 生成代码覆盖率报告
# FORCE: ;
# $(GCOV_FULL): FORCE
# 	lcov -d $(OUT_DIR) -b . -c -o $@
# $(GCOV_LEAN): $(GCOV_FULL)
# 	lcov -r $< -o $@ '*/headers/*' '/usr/*'
# $(COV_DIR): $(GCOV_LEAN)
# 	genhtml -o $@ $<



-include $(ALL_DEPS)
