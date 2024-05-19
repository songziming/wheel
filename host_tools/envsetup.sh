#!/bin/bash

# 准备编译环境

TMP_DIR=$HOME/projects
BIN_DIR=$HOME/.apps

sudo apt install git build-essential cmake swig libedit-dev
sudo apt install autoconf automake autopoint flex bison libtool pkg-config

mkdir -p $TMP_DIR

# build LLVM
cd $TMP_DIR
git clone --depth=1 https://github.com/llvm/llvm-project.git
mkdir build-llvm && cd build-llvm
cmake ../llvm-project/llvm \
    -DCMAKE_INSTALL_PREFIX=$BIN_DIR/llvm \
    -DLLVM_ENABLE_PROJECTS="clang;lld;compiler-rt;lldb" \
    -DLLVM_TARGETS_TO_BUILD=X86 \
    -DCMAKE_BUILD_TYPE=Release \
    -DLLDB_ENABLE_LIBEDIT=On
cmake --build . --parallel 8
cmake --build . --target install

# build GRUB
cd $TMP_DIR
git clone --depth=1 https://git.savannah.gnu.org/git/grub.git
cd grub
./bootstrap
./configure --prefix=$BIN_DIR/grub --target=x86_64 --with-platform=efi
make -j8
make install
# 如果安装到非标准位置，locale 会找不到，可以创建一个符号链接
ln -sf /usr/share/locale $BIN_DIR/grub/share/locale
