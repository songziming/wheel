# 从源码编译 QEMU

Windows 系统需要使用 MSYS2-MINGW64。

~~~bash
./configure --prefix=/d/qemu_mingw64 --enable-sdl --enable-gtk --target-list=x86_64-softmmu --disable-werror --enable-strip
make
make install
~~~


# 编译 bochs（Windows）

从 sourceforge 下载 msvc 版本源码，解压。

使用 VS 打开 vs2019/bochs.sln 解决方案，升级到最新版本。

编辑 bochs 子项目中的 config.h 文件，修改以下的宏：

~~~c
#define BX_USE_CPU_SMF 0
#define BX_DEBUGGER 1
#define BX_DEBUGGER_GUI 1
#define BX_SUPPORT_HANDLERS_CHAINING_SPEEDUPS 0
#define BX_SUPPORT_SMP 1
~~~

生成解决方案，输出文件位于 obj-release，只需把 bochs.exe 一个文件拷贝出来即可。


