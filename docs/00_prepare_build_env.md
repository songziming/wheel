# 准备开发环境

需要使用 Linux 或 WSL，使用 clang 编译内核，使用 grub 引导器。

如果是 UEFI Linux，则系统已经安装了 grub，但缺少 i386-pc 相关文件，使用下面的命令安装：

~~~
sudo apt install grub-pc-bin
~~~

如果是 WSL/Docker，则需要从源码编译安装 grub。
