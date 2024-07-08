# OS 开发容器，包括内核编译工具链 LLVM、引导器 GRUB

ARG NJOBS=8

FROM ubuntu:24.04

ENV TZ=Asia/Shanghai

WORKDIR /root

RUN ln -snf /usr/share/zoneinfo/$TZ /etc/localtime
RUN echo $TZ > /etc/timezone

RUN apt update && \
    apt install -y --reinstall ca-certificates && \
    apt install -y --no-install-recommends \
        git gawk gettext pkg-config build-essential cmake python3 \
        autoconf automake autopoint \
        flex bison libfreetype-dev fonts-unifont \
        fdisk dosfstools mtools xorriso && \
    rm -rf /var/lib/apt/lists/*

# RUN wget https://github.com/llvm/llvm-project/releases/download/llvmorg-18.1.6/llvm-project-18.1.6.src.tar.xz
# RUN tar -xf llvm-project-18.1.6.src.tar.xz

# RUN wget https://ftp.gnu.org/gnu/grub/grub-2.12.tar.xz
# RUN tar -xf grub-2.12.tar.xz
# RUN sed -i '/EXTRA_DIST += grub-core\/genemuinitheader.sh/a EXTRA_DIST += grub-core\/extra_deps.lst' \
#     grub-2.12/conf/Makefile.extra-dist

# 编译内核工具链
RUN git clone --depth=1 https://github.com/llvm/llvm-project.git            && \
    mkdir build && cd build                                                 && \
    cmake ../llvm-project/llvm \
        -DLLVM_ENABLE_PROJECTS="clang;lld;compiler-rt" \
        -DLLVM_TARGETS_TO_BUILD=X86 \
        -DCMAKE_BUILD_TYPE=Release                                          && \
    cmake --build . --parallel ${NJOBS}                                     && \
    cmake --build . --target install                                        && \
    cd .. && rm -rf llvm-project build

# 编译引导器 GRUB，包括 bios 和 uefi 两个版本
RUN git clone --depth=1 https://git.savannah.gnu.org/git/grub.git           && \
    cd grub && ./bootstrap                                                  && \
    mkdir build_bios && cd build_bios                                       && \
    ./configure --target=i386 --with-platform=pc                            && \
    make -j ${NJOBS}                                                        && \
    make install                                                            && \
    cd .. && mkdir build_uefi && cd build_uefi                              && \
    ./configure --target=x86_64 --with-platform=efi --enable-grub-mkfont    && \
    make -j ${NJOBS}                                                        && \
    make install                                                            && \
    cd ../.. && rm -rf grub

# 准备字体文件，不然 grub-mkrescue/grub-install 会报错
RUN grub-mkfont -o /usr/local/share/grub/unicode.pf2 /usr/share/fonts/opentype/unifont/unifont.otf

# grub 需要一个目录，需要手动创建
# 如果不创建这个目录，就要在执行 grub-mkrescue/grub-install 时指定参数 --locale-directory=/usr/share/locale
RUN ln -sf /usr/share/locale /usr/local/share/locale

# # 创建 uefi 引导器，就是一个普通文件，拷贝到 FAT32 分区就可以
# COPY grub.cfg /root/grub.cfg
# RUN grub-mkstandalone -O x86_64-efi  -o BOOTX64.EFI "boot/grub/grub.cfg=grub.cfg"
