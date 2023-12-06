@ qemu-system-x86_64 -cpu max -smp 4 -m 256 -serial stdio ^
    -device isa-debug-exit,iobase=0xf4,iosize=0x04 ^
    -cdrom build\wheel.iso -gdb tcp::4444

@ echo return value %ERRORLEVEL%
