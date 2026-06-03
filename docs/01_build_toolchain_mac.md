# macos 开发环境准备

这里默认是 apple silicon macos

~~~bash
brew install gmake llvm lld pkgconf googletest
brew install i686-elf-grub qemu gdb
~~~

macos 自带的 make 版本太老，许多特性都不支持，将 brew 安装的新版作为默认：

~~~bash
# 添加到 ~/.zshrc 末尾
export PATH="/opt/homebrew/opt/make/libexec/gnubin:$PATH"
~~~

## 使用的工具链

有多种选择：
- apple clang，即 xcode 自带的
- homebrew clang，即使用 brew 安装的
- cross clang，即自己编译的

macos 运行在 arm64，但我们的 OS 面向 x86，所以需要交叉编译
幸好 clang 天然就是交叉编译器，指定 -target x86_64-none-elf 即可

经测试，使用 xcode 自带工具链就可以编译，而且比自己编译的 llvm 问题更少，且支持 asan

## 编译 LLVM 工具链

Apple Silicon 开发 x86 os 也需要交叉编译器
使用 brew 安装的 llvm 只能用来开发 arm64，无法用于交叉编译 x86_64
还是需要自己构建一套 llvm 工具链

编译 llvm 的命令有一些区别。
注意编译 llvm 使用的应该是 apple clang，也就是 xcode 自带的工具链。
编译单元测试用的也是 apple clang。

~~~bash
cmake ../llvm-project/llvm \
    -G Ninja \
    -DCMAKE_INSTALL_PREFIX=/opt/llvm \
    -DLLVM_ENABLE_PROJECTS="clang;clang-tools-extra;lld;compiler-rt" \
    -DLLVM_TARGETS_TO_BUILD="X86;AArch64" \
    -DCMAKE_BUILD_TYPE=Release
ninja
sudo ninja install
~~~

自己编译的 clang 只用来编译内核镜像
