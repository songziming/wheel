@ setlocal

@ if exist build/hd.img (
    set hard_drive=-drive file=build/hd.img,format=raw
)

qemu-system-x86_64 -cpu max -smp 4 -m 256 ^
    %hard_drive% -cdrom build/cd.iso -boot d ^
    -device isa-debug-exit,iobase=0xf4,iosize=0x04 ^
    -vga vmware -gdb tcp::4444 -serial stdio

@ endlocal
