KERN_CFLAGS  += -mcmodel=kernel -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -mno-3dnow -mno-fma
KERN_LDFLAGS += -z max-page-size=0x1000
