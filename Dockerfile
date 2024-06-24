# how to use docker
# 使用 image 创建后台容器：docker run -dit osdev


FROM ubuntu:24.04

ENV TZ=Asia/Shanghai

RUN ln -snf /usr/share/zoneinfo/$TZ /etc/localtime
RUN echo $TZ > /etc/timezone

RUN apt update
RUN apt upgrade -y
RUN apt install -y git build-essential wget cmake python3


# 编译 grub 需要的依赖项
RUN apt install -y flex bison autoconf automake autopoint gettext libtool pkg-config gawk
RUN apt install -y libdevmapper-dev liblzma-dev libfuse-dev libfuse3-dev

# RUN apt install -y mtools xorriso


WORKDIR /root

# 下载工具链，从源码编译
RUN wget https://github.com/llvm/llvm-project/releases/download/llvmorg-18.1.6/llvm-project-18.1.6.src.tar.xz
RUN tar -xf llvm-project-18.1.6.src.tar.xz
RUN mkdir build-llvm && pushd build-llvm
RUN cmake ../llvm-project-18.1.6.src/llvm \
    -DLLVM_ENABLE_PROJECTS="clang;lld;compiler-rt" \
    -DLLVM_TARGETS_TO_BUILD=X86 \
    -DCMAKE_BUILD_TYPE=Release
RUN cmake --build . --parallel
RUN cmake --build . --target install
RUN popd
RUN rm -rf build-llvm llvm-project-18.1.6.src

# # 下载 GRUB，从源码编译
# # 这个版本的 grub 编译脚本有错误，缺少 extra_deps.lst 的编译规则
# RUN wget https://ftp.gnu.org/gnu/grub/grub-2.12.tar.xz
# RUN tar -xf grub-2.12.tar.xz
# RUN cd grub-2.12
# RUN sed -i '/EXTRA_DIST += grub-core\/genemuinitheader.sh/a EXTRA_DIST += grub-core\/extra_deps.lst' conf/Makefile.extra-dist

# 从 git 获取 grub 代码，编译 64-bit uefi 和 32-bit bios 两个版本
RUN git clone --depth=1 https://git.savannah.gnu.org/git/grub.git
RUN pushd grub
RUN ./bootstrap
RUN ./configure --target=x86_64 --with-platform=efi
RUN make
RUN make install
RUN ./configure --target=i386 --with-platform=pc
RUN make clean
RUN make
RUN make install
RUN popd
RUN rm -rf grub
