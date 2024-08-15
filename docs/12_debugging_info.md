以调试模式编译的 ELF 文件包含调试信息，这些信息可以被 gdb 使用，但是 kernel 不能直接使用调试器。

调试信息最大的帮助是，发生错误时，可以根据内存地址定位到出错的内存行。

GCC 提供了一个命令行工具 `addr2line`，可以将地址转换成文件名与行号，但我们希望内核可以自己转换。

`objdump -g` 可以将所有调试信息打印出来。

# 识别代码行号

行号信息来自 `.debug_line` section。

如何识别这个 section？我们识别符号表用的是 section type，但是似乎没有找到 debug_line 的类型定义。是否只能根据 section 名称判断？是否说明如果缺少字符串表，gdb 就无法解析调试信息？

经测试，这些 section 类型是 progbits，和代码数据一样被加载到内存。

ELF 文件里，这些 debug section 填写的 addr=0，表示链接器没有限制这些 section 应该加载到的地址，因此加载到哪里由 grub 决定。
经测试，发现这些 debug section 被见缝插针地放在代码数据段之间。说明在解析 debug section 之后，需要把这些内容备份下来。
