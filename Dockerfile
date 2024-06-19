FROM ubuntu:24.04

ENV TZ=Asia/Shanghai

RUN ln -snf /usr/share/zoneinfo/$TZ /etc/localtime
RUN echo $TZ > /etc/timezone

RUN apt update
RUN apt upgrade -y
RUN apt install -y git build-essential wget cmake python3
# RUN apt install -y mtools xorriso

# 下载工具链，从源码编译
WORKDIR /root
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

# 下载 GRUB，从源码编译