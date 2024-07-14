# build script for wheel kernel

DEBUG ?= 1
UBSAN ?= $(DEBUG)
KASAN ?= 0 #$(DEBUG)
KTEST ?= $(DEBUG)

ARCH  ?= x86_64

CC := clang --target=$(ARCH)-pc-none-elf
LD := ld.lld

OUT_DIR := build
ISO_DIR := $(OUT_DIR)/iso
OUT_ELF := $(OUT_DIR)/wheel.elf
OUT_MAP := $(OUT_DIR)/wheel.map
OUT_ISO := $(OUT_DIR)/cd.iso
OUT_IMG := $(OUT_DIR)/hd.img

################################################################################

KERNEL := kernel_new

KSUBDIRS := $(patsubst %/,%,$(wildcard $(KERNEL)/*/))
KSUBDIRS := $(filter-out $(wildcard $(KERNEL)/1_arch*), $(KSUBDIRS))
KSUBDIRS := $(KERNEL)/1_arch_$(ARCH) $(KSUBDIRS)

KSOURCES := $(foreach d,$(KSUBDIRS),$(shell find $(d) -name "*.S" -o -name "*.c"))
KOBJECTS := $(patsubst $(KERNEL)/%,$(OUT_DIR)/%.o,$(KSOURCES))

KDEPENDS := $(patsubst %.o,%.d,$(KOBJECTS))
KOBJDIRS := $(sort $(dir $(KDEPENDS)))


CFLAGS := -std=c11 $(KSUBDIRS:%=-I%) -Wall -Wextra -Wshadow -Werror=implicit
CFLAGS += -ffreestanding -fno-builtin -flto -ffunction-sections -fdata-sections

ifeq ($(DEBUG),1)
    CFLAGS += -g -DDEBUG -fstack-protector -fno-omit-frame-pointer
else
    CFLAGS += -O2 -DNDEBUG
endif

ifeq ($(UBSAN),1)
    CFLAGS += -fsanitize=undefined
endif

ifeq ($(KASAN),1)
    CFLAGS += -fsanitize=kernel-address
    CFLAGS += -mllvm -asan-mapping-offset=0xdfffe00000000000
    CFLAGS += -mllvm -asan-globals=false
endif

LAYOUT := $(KERNEL)/1_arch_$(ARCH)/layout.ld
LFLAGS := -nostdlib --gc-sections -Map=$(OUT_MAP) -T $(LAYOUT) --no-warnings

DEPGEN = -MT $@ -MMD -MP -MF $(patsubst %.o,%.d,$@)

include $(KERNEL)/1_arch_$(ARCH)/config.mk

################################################################################

.PHONY: all elf iso img clean

all: elf iso img
elf: $(OUT_ELF)
iso: $(OUT_ISO)
img: $(OUT_IMG)

clean:
	rm -rf $(OUT_DIR)

################################################################################

$(KOBJECTS): | $(KOBJDIRS)

$(OUT_DIR)/%/:
	mkdir -p $@

$(OUT_DIR)/%.S.o: $(KERNEL)/%.S
	$(CC) -c -DS_FILE $(CFLAGS) $(DEPGEN) -o $@ $<

$(OUT_DIR)/%.c.o: $(KERNEL)/%.c
	$(CC) -c -DC_FILE $(CFLAGS) $(DEPGEN) -o $@ $<

$(OUT_ELF): $(KOBJECTS)
	$(LD) $(LFLAGS) -o $@ $^

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

-include $(KDEPENDS)
