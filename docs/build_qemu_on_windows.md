在 windows 下编译 qemu

需要在 msys2 环境下编译，安装如下依赖项：

```bash
pacman -S base-devel ninja git \
    mingw-w64-x86_64-clang \
    mingw-w64-x86_64-python3 \
    glib2-devel
```

注意应该选择环境 MINGW64，只有这个环境编译出来的软件才能脱离 MSYS2 运行

然后进入 qemu 目录：

```bash
./configure --prefix=/d/qemu --target-list=x86_64-softmmu --static
make -j
make install
```
