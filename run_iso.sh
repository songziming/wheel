#!/bin/bash

qemu-system-x86_64 -nographic \
    -cpu qemu64 -smp 4 -m 256 \
    -device isa-debug-exit,iobase=0xf4,iosize=0x04 \
    -cdrom build/wheel.iso -gdb tcp::4444
