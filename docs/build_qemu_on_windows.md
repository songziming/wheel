在 windows 下编译 qemu

需要在 msys2 环境下编译，安装如下依赖项：

```bash
pacman -S base-devel mingw-w64-x86_64-clang ninja git
```

注意应该选择环境 MINGW64，只有这个环境编译出来的软件才能脱离 MSYS2 运行

然后进入 qemu 目录：

```bash
./configure --prefix=/d/qemu
make -j
make install
```