@ setlocal

@ if exist build/hd.img (
    set HARD_DRIVE=-drive file=build/hd.img,format=raw
)

qemu-system-x86_64 -cpu max -smp 4 -m 256 ^
    %HARD_DRIVE% -cdrom build/cd.iso -boot d ^
    -device isa-debug-exit,iobase=0xf4,iosize=0x04 ^
    -vga std -gdb tcp::4444 -serial stdio

@REM -vga vmware 可以使用 vmware-svga

@ endlocal
