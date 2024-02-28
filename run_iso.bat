@ qemu-system-x86_64 -cpu max -smp 4 -m 256 -serial stdio -vga vmware ^
    -drive file=c.img,format=raw -cdrom build\wheel.iso -gdb tcp::4444 ^
    -device isa-debug-exit,iobase=0xf4,iosize=0x04

@ echo return value %ERRORLEVEL%
