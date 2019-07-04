Wheel Operating System
======================

Wheel is an operating system for 64-bit Intel/AMD architecture, written from scratch.

Required tools and softwares:

- Linux/Unix environment, WSL (Windows Subsystem for Linux) also works.
- GCC cross compiler and binutils for x86_64 target.
- `grub`, `xorriso` and `mtools` to make bootable iso image (also install `grub-pc-bin` on EFI systems).
- `qemu` (or other virtual machine) to run the system.

To build and run wheel:

- `make DEBUG=1` to generate kernel image `bin/wheel.bin`.
- `make iso` to create ISO image `bin/wheel.iso`.
- `make run` to run the OS with QEMU.
- `make clean` to delete all generated files.
