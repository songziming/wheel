# OS 开发容器，包括内核编译工具链 LLVM、引导器 GRUB

ARG NJOBS=8

FROM ubuntu:24.04

ENV TZ=Asia/Shanghai

RUN ln -snf /usr/share/zoneinfo/$TZ /etc/localtime
RUN echo $TZ > /etc/timezone

RUN apt update
RUN apt install -y git gawk build-essential cmake python3
RUN apt install -y autoconf automake autopoint gettext pkg-config
RUN apt install -y flex bison libfreetype-dev fonts-unifont
RUN apt install -y fdisk dosfstools mtools xorriso

# RUN wget https://github.com/llvm/llvm-project/releases/download/llvmorg-18.1.6/llvm-project-18.1.6.src.tar.xz
# RUN tar -xf llvm-project-18.1.6.src.tar.xz

# RUN wget https://ftp.gnu.org/gnu/grub/grub-2.12.tar.xz
# RUN tar -xf grub-2.12.tar.xz
# RUN sed -i '/EXTRA_DIST += grub-core\/genemuinitheader.sh/a EXTRA_DIST += grub-core\/extra_deps.lst' \
#     grub-2.12/conf/Makefile.extra-dist

# 获取源码，准备临时编译目录
WORKDIR /root
RUN git clone --depth=1 https://github.com/llvm/llvm-project.git
RUN git clone --depth=1 https://git.savannah.gnu.org/git/grub.git
RUN mkdir build_llvm
RUN mkdir build_grub_bios
RUN mkdir build_grub_uefi

# 构建内核工具链
WORKDIR build_llvm
RUN cmake ../llvm-project/llvm \
    -DLLVM_ENABLE_PROJECTS="clang;lld;compiler-rt" \
    -DLLVM_TARGETS_TO_BUILD=X86 \
    -DCMAKE_BUILD_TYPE=Release
RUN cmake --build . --parallel ${NJOBS}
RUN cmake --build . --target install

# 构建引导器（bios）
WORKDIR build_grub_bios
RUN ../configure --target=i386 --with-platform=pc
RUN make -j ${NJOBS}
RUN make install

# 构建引导器（uefi）
WORKDIR build_grub_uefi
RUN ../configure --target=x86_64 --with-platform=efi --enable-grub-mkfont
RUN make -j ${NJOBS}
RUN make install

# 删除临时目录
WORKDIR /root
RUN rm -rf llvm-project grub build_*

# 准备字体文件，不然 grub-mkrescue/grub-install 会报错
RUN grub-mkfont -o /usr/local/share/grub/unicode.pf2 /usr/share/fonts/opentype/unifont/unifont.otf

# grub 需要一个目录，需要手动创建
# 如果不创建这个目录，就要在执行 grub-mkrescue/grub-install 时指定参数 --locale-directory=/usr/share/locale
RUN ln -sf /usr/share/locale /usr/local/share/locale

# # 创建 uefi 引导器，就是一个普通文件，拷贝到 FAT32 分区就可以
# COPY grub.cfg /root/grub.cfg
# RUN grub-mkstandalone -O x86_64-efi  -o BOOTX64.EFI "boot/grub/grub.cfg=grub.cfg"
