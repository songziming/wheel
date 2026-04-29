# Wheel Operating System

![test status](https://github.com/songziming/wheel/actions/workflows/kernel-unit-test.yml/badge.svg)

Wheel is an operating system, written from scratch.

64-bit higher half kernel, multiprocessor support, preemptive scheduling, ring-0 only, simple kernel shell.

## build and test

Wheel is build using LLVM under Linux, ses [how to build toolchain](./docs/00_build_toolchain.md).

```bash
make        # build kernel elf image build/wheel.elf
make iso    # create bootable iso image build/wheel.iso
make unit   # build unit test binary build/unit
make cov    # run unit test and generate coverage report in build/coverage
```

Launch QEMU using `./run_iso.sh` (or `run_iso.bat` under Windows).
