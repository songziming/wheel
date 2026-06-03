# 准备开发工具

在全新的 Debian 发行版上执行：

~~~
sudo apt install clang lld llvm grub-pc-bin xorriso libgtest-dev
sudo apt install grub-pc dosfstools mtools # 创建硬盘镜像需要
~~~

直接使用系统的包管理器就可以。

## 编译 llvm 工具链

选择 clang 作为编译器，因为 clang 本身就是交叉编译器。
使用包管理器安装的 clang 应该也可以，但自己编译的更保险。

~~~bash
git clone --depth=1 https://github.com/llvm/llvm-project.git
mkdir build_llvm
cd build_llvm
cmake ../llvm-project/llvm \
    -DCMAKE_INSTALL_PREFIX=/opt/llvm \
    -DLLVM_ENABLE_PROJECTS="clang;lld;compiler-rt" \
    -DLLVM_TARGETS_TO_BUILD=X86 \
    -DCMAKE_BUILD_TYPE=Release
make -j 8
make install
~~~

## 编译 GRUB

如果使用 Linux，系统已经安装了 grub，只需安装 i386-pc 相关文件：

~~~bash
sudo apt install grub-pc-bin
~~~

从源码编译 grub，需要编译 bios 和 uefi 两个版本：

~~~bash
git clone --depth=1 https://git.savannah.gnu.org/git/grub.git
cd grub
./bootstrap

mkdir build_bios
pushd build_bios
../configure --prefix=/opt/grub --target=i386 --with-platform=pc
make -j 8
make install
popd

mkdir build_uefi
pushd build_uefi
../grub/configure --prefix=/opt/grub --target=x86_64 --with-platform=efi --enable-grub-mkfont
make -j 8
make install
popd

# 还缺少一个字体文件，否则运行会出错
ln -sf /usr/share/locale /opt/grub/usr/local/share/locale
grub-mkfont -o /opt/grub/usr/local/share/grub/unicode.pf2 /usr/share/fonts/opentype/unifont/unifont.otf
~~~
