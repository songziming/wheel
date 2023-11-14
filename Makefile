# build script for wheel os

DEBUG   ?= 1
ARCH    := x86_64

OUT_DIR := build
OBJ_DIR := $(OUT_DIR)/objs

OUT_MAP := $(OUT_DIR)/wheel.map



define get_subdirs
    $(foreach d,$(1),$(wildcard $(d)/$(2)))
endef

define get_sources
    $(foreach d,$(1),$(shell find $(d) -name "*.S" -o -name "*.c" -o -name "*.cc"))
endef

define all_files
    $(call get_sources,$(call get_subdirs,$(1),$(2)))
endef

# define to_objects
#     $(patsubst %,$(OBJ_DIR)/%.$(2),$(1))
# endef

# # all_objects modules, subdir, suffix
# define all_objects
#     $(call to_objects,$(call get_sources,$(call get_subdirs,$(1),$(2))),$(3))
# endef



KDIRS := kernel_1/arch_$(ARCH)
KDIRS += $(filter-out $(wildcard kernel_1/arch_*),$(wildcard kernel_1/*))
KINCS := $(patsubst %,%/headers,$(KDIRS)) kernel_1
KOBJS := $(patsubst %,$(OBJ_DIR)/%.ko,$(call all_files,$(KDIRS),sources))


KCFLAGS := -std=c11 $(KINCS:%=-I%) -Wall -Wextra -Wshadow -Werror=implicit
KCFLAGS += -ffreestanding -fno-builtin -flto -ffunction-sections -fdata-sections
ifeq ($(DEBUG),1)
    KCFLAGS += -g -DDEBUG
else
    KCFLAGS += -O2 -DNDEBUG
endif


KLAYOUT := kernel_1/arch_$(ARCH)/layout.ld
KLFLAGS := -nostdlib --gc-sections -Map=$(OUT_MAP) -T $(KLAYOUT) --no-warnings


TOBJS := $(patsubst %,$(OBJ_DIR)/%.to,$(call all_files,$(KDIRS),tests))

TCFLAGS := -g -DUNIT_TEST $(KINCS:%=-I%) -I tools/kernel_test
TCFLAGS += -fsanitize=address -fprofile-arcs -ftest-coverage



.PHONY: debug

debug:
	@ echo "kernel objects: $(KOBJS)"
	@ echo "unit test objects: $(TOBJS)"
