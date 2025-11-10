# 内核调试手段

## 使用 gdb 调试在 QEMU 中运行的内核

运行 QEMU 时传入参数 `-gdb tcp::4444`（run_iso 的默认行为）

启动 gdb，执行如下命令：

~~~bash
(gdb) target remote localhost:4444  # 连接到 qemu
(gdb) symbol-file build/wheel.elf   # 指定符号文件
(gdb) b handle_assert_fail          # 添加断点
(gdb) c                             # 让 qemu 继续执行
~~~

## gdb 访问 percpu var

thiscpu 变量需要偏移 gs.base，gdb 可以访问这个寄存器：

~~~bash
(gdb) p (*(task_t**)((size_t)&g_tid_prev + $gs_base))
~~~

## Bochs 使用符号名

Bochs 不像 gdb，不能解析 elf 文件里的符号表。

需要生成文本格式的符号表，每行对应一个符号：

~~~bash
nm build/wheel.elf | awk '{ print $1" "$3 }' > build/wheel.sym
~~~

在 Bochs 内部，使用命令加载符号文件：

~~~
ldsym global "build/wheel.sym"
~~~

## 根据内存地址查询代码行号

~~~bash
llvm-addr2line -e build/wheel.elf 0xdeadbeef
~~~
