@ if "%*" == "run" (
    wsl make iso && qemu-system-x86_64 -smp 4 -m 64 ^
        -drive file=c.img,format=raw -cdrom bin\wheel.iso ^
        -vga vmware -serial stdio -gdb tcp::4444 -boot order=d
) else (
    wsl make %*
)
