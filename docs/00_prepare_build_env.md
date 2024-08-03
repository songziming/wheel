# 准备开发环境

需要使用 Linux 或 WSL，使用 clang 编译内核，使用 grub 引导器。

## 准备 LLVM 工具链

我们使用 clang 编译内核，好处是 clang 本身就是检查编译器。

从 github 获取最新的源码：

~~~bash
git clone --depth=1 https://github.com/llvm/llvm-project.git
~~~

只编译我们需要的工具和平台：

~~~bash
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

## 准备 GRUB 引导器

如果开发环境是 UEFI Linux，则系统已经安装了 grub，只是缺少 i386-pc 相关文件，使用下面的命令安装：

~~~bash
sudo apt install grub-pc-bin
~~~

如果开发环境是 WSL 或 Docker，则需要从源码编译安装 grub。

使用 git 获取源码，并使用 bootstrap 生成 configure 脚本：

~~~bash
git clone --depth=1 https://git.savannah.gnu.org/git/grub.git
cd grub
./bootstrap
~~~

配置并编译，包括 bios 和 uefi 两个版本：

~~~bash
mkdir build_bios
cd build_bios
../grub/configure --prefix=/opt/grub --target=i386 --with-platform=pc
make -j 8
make install
~~~

~~~bash
mkdir build_uefi
cd build_uefi
../grub/configure --prefix=/opt/grub --target=x86_64 --with-platform=efi --enable-grub-mkfont
make -j 8
make install
~~~

还需要创建一个符号链接，准备一个字体文件，否则grub运行会出错：

~~~bash
ln -sf /usr/share/locale /opt/grub/usr/local/share/locale
grub-mkfont -o /opt/grub/usr/local/share/grub/unicode.pf2 /usr/share/fonts/opentype/unifont/unifont.otf
~~~
