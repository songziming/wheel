# makefile for wheel kernel

ELFFILE :=  $(OUTDIR)/wheel.elf
BINFILE :=  $(OUTDIR)/wheel.bin
MAPFILE :=  $(OUTDIR)/wheel.map
SYMFILE :=  $(OUTDIR)/wheel.sym

SUBDIRS :=  arch/$(ARCH) core mem drvs fs misc
SRCLIST :=  $(foreach d,$(SUBDIRS),$(shell find $(d) -type f -name '*.S' -o -name '*.c'))
OBJLIST :=  $(patsubst %,$(OUTDIR)/%.o,$(SRCLIST))
DEPLIST :=  $(patsubst %.c.o,%.c.d,$(filter %.c.o,$(OBJLIST)))

CFLAGS  :=  -c -std=c11 -Ih -Ih/arch/$(ARCH) -DKERNEL -DARCH=$(ARCH)
CFLAGS  +=  -Wall -Wextra -Werror=implicit -Wno-cast-function-type
CFLAGS  +=  -ffreestanding -ffunction-sections -fdata-sections

ifeq ($(DEBUG), 1)
	CFLAGS  +=  -g -DDEBUG
else
	CFLAGS  +=  -g -DNDEBUG
endif

DEPGEN   =  -MT $@ -MMD -MP -MF $(basename $@).t
LFLAGS  :=  -nostdlib -lgcc -T h/arch/$(ARCH)/layout.ld -Wl,-Map=$(MAPFILE),--gc-sections

include h/arch/$(ARCH)/config.mk

build:  $(BINFILE) $(SYMFILE)

clean:
	@ rm -f $(OBJLIST)
	@ rm -f $(DEPLIST)
	@ rm -f $(ELFFILE)
	@ rm -f $(BINFILE)
	@ rm -f $(MAPFILE)
	@ rm -f $(SYMFILE)

$(ELFFILE): $(OBJLIST)
	@ echo "[K:LD] $@"
	@ mkdir -p $(@D) > /dev/null
	@ $(CC) $(LFLAGS) -o $@ $^

$(BINFILE): $(ELFFILE)
	@ echo "[K:CP] $@"
	@ mkdir -p $(@D) > /dev/null
	@ $(OBJCOPY) --strip-debug $< $@

$(SYMFILE): $(ELFFILE)
	@ echo "[K:CP] $@"
	@ mkdir -p $(@D) > /dev/null
	@ $(OBJCOPY) --only-keep-debug $< $@

$(OUTDIR)/%.S.o: %.S
	@ echo "[K:AS] $@"
	@ mkdir -p $(@D) > /dev/null
	@ $(CC) $(CFLAGS) -o $@ $<

$(OUTDIR)/%.c.o: %.c $(OUTDIR)/%.c.d
	@ echo "[K:CC] $@"
	@ mkdir -p $(@D) > /dev/null
	@ $(CC) $(CFLAGS) $(DEPGEN) -o $@ $<
	@ mv -f $(basename $@).t $(basename $@).d

$(DEPLIST): ;
.PRECIOUS: $(DEPLIST)

-include $(DEPLIST)
