#!/bin/bash

qemu-system-x86_64 -cpu qemu64 -smp 4 -m 256 -serial stdio -vga vmware \
    -drive file=build/hd.img,format=raw -cdrom build/cd.iso -boot d \
    -device isa-debug-exit,iobase=0xf4,iosize=0x04 -gdb tcp::4444
