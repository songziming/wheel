# Wheel Operating System

Wheel is an operating system, written from scratch.

### prepare toolchain

```bash
cd /path/to/llvm-project
mkdir build-llvm && cd build-llvm
cmake ../llvm \
    -DCMAKE_INSTALL_PREFIX=/path/to/cross \
    -DLLVM_ENABLE_PROJECTS="clang;lld;compiler-rt;lldb" \
    -DLLVM_TARGETS_TO_BUILD=X86 \
    -DCMAKE_BUILD_TYPE=Release
cmake --build . --parallel
cmake --build . --target install
```
