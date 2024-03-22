# Wheel Operating System

Wheel is an operating system, written from scratch.

64-bit higher half kernel, multiprocessor support, preemptive scheduling, ring-0 only, simple kernel shell.

![kernel shell](./docs/kernel_shell_blocks.png)

## prepare toolchain

Build wheel from Linux or WSL.

```bash
cd /path/to/llvm-project
mkdir build-llvm && cd build-llvm
cmake ../llvm \
    -DCMAKE_INSTALL_PREFIX=/path/to/cross \
    -DLLVM_ENABLE_PROJECTS="clang;lld;compiler-rt;lldb" \
    -DLLVM_TARGETS_TO_BUILD=X86 \
    -DCMAKE_BUILD_TYPE=Release \
    -DLLDB_ENABLE_LIBEDIT=On
cmake --build . --parallel
cmake --build . --target install
```

## build and test

```bash
make        # compile kernel elf image build/wheel.elf
make iso    # create bootable iso image build/wheel.iso
make test   # compile unit test binary build/test
make cov    # run unit test and generate coverage report in build/cov
```

Launch QEMU using `./run_iso.sh` (or `run_iso.bat` under Windows).
