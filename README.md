# Wheel Operating System

Wheel is an operating system, written from scratch.

Features:
- [x] 64-bit mode
- [x] multiprocessor support
- [ ] preemptive scheduling

Limitations:
- max CPU number: 64
- max physical memory: 16TB-4KB

## prepare toolchain

Wheel uses LLVM.

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
python3 -m http.server -d build/cov
```

## create disk image

```bash
dd if=/dev/zero of=c.img bs=1M count=64
```

## debug using Qemu and gdb

Launch qemu with `./run_iso.sh`.

And run gdb in another terminal (requires newer version):

```bash
(gdb) target remote localhost:4444
(gdb) symbol-file build/wheel.elf
(gdb) b handle_assert_fail
```

## debug using Bochs

Extract symbol list from kernel image:

```bash
nm build/wheel.elf | awk '{ print $1" "$3 }' > build/wheel.sym
```

In bochs, use the following command to load symbol file:

```
ldsym global "build/wheel.sym"
```
