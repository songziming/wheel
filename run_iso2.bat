@ qemu-system-x86_64 -cpu max -smp 4 -m 256 -vga vmware ^
    -chardev stdio,id=char0,signal=off -serial chardev:char0 ^
    -drive file=build/hd.img,format=raw -cdrom build/cd.iso -boot d ^
    -device isa-debug-exit,iobase=0xf4,iosize=0x04 -gdb tcp::4444
