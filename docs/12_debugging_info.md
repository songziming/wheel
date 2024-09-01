以调试模式编译的 ELF 文件包含调试信息，这些信息可以被 gdb 使用，但是 kernel 不能直接使用调试器。

调试信息最大的帮助是，发生错误时，可以根据内存地址定位到出错的内存行。

GCC 提供了一个命令行工具 `addr2line`，LLVM 也有对应的程序 `llvm-addr2line`，可以将地址转换成文件名与行号，但是这需要 qemu 打印出内存地址后单独执行命令，无法在内核运行状态下获取行号。

# 解析调试信息（行号信息）

如果希望内核能直接打印代码行号，就需要解析 dwarf 调试信息，特别是 .debug_line。

使用 `objdump -g` 可以将所有调试信息打印出来。

这里基于 Dwarf v5，不同版本记录行号信息的方式不同。Dwarf v5 使用一套自己发明的指令集动态生成 addr2line 对应关系表。

## debug_line 大致结构

debug_line 由多个 unit 组成

每个 unit 包括两部分：
- 开头的 line number program header
- 之后的 line number program opcodes

每个指令可以有如下行为（之一或都有）：
- 更新状态机
- addr2line 矩阵中新增一行，内容就是当前状态机

理论中的 addr2line 矩阵是这样的：

| addr | file | line | column |
|:----:|:----:|:----:|:------:|
|  X   |  F   |  L   |   C    |
|  Y   |      |      |        |

表示内存地址 `[X,Y)` 之间对应的代码是文件 F 的第 L 行。

## 如何解析 debug_line

### 静态映射

系统启动阶段解析 debug_line，构建完整的 addr2line 表格。

占用内存较大，可以使用游程编码压缩。

或者以 sequence 为单位保存，不直接保存矩阵，而是保存两行的增量（addr-advance, line-advance）

### 动态映射

dwarf 使用 opcode 编码表示行号信息，已经是最大程度压缩存储了。

因此，查询某个内存地址对应的行号时，只需找到包含这个地址的 unit，解释执行这个 unit 里面的 opcodes。

我们只需要将 debug_line 和相关 section 保存下来（例如 debug_str、debug_line_str），随用随分析，比较节省空间。而且系统运行中，只有很少的时候需要查询行号，耗时多一点也能接受。

问题在于，每个 sequence 没有记录明确的内存地址范围，必须执行一遍 opcodes 才能知道。我们可以建立一套 sequence 快速索引表，根据地址先找出 sequence，定位到 opcodes，然后再执行这段 opcodes 确定具体行号。

# 详细解析过程

## 需要关注的指令

- set file
- set address (PC)
- advance line
- advance PC
- special op: advance both line and PC
- end of sequence

## 状态机

执行 opcodes 的过程就是更新状态机，主要字段：

- address，机器指令内存地址
- op_index，指令内部 operation 编号，只有 VLIW 架构才需要这个字段，非 VLIW 架构下该字段一直为 0
- file，文件编号
- line，行号，从 1 开始
- column，列号

- is_stmt，当前指令能否加断点
- basic_block，当前指令是否为一个语句块的开头
- end_sequence，当前指令是否为序列结束后的下一个指令（开区间）
- prologue_end，当前指令是否对应某个函数体的第一行。
    - 有些函数需要若干指令准备局部变量和其他资源，这些指令程作 prologue
    - prologue 完全是编译器自动生成的，不对应源码
    - 如果用户添加函数断点，应该在 prologue 结束位置停下
- epilogue_begin，当前指令是函数体返回前的最后一条指令
    - 类似于 prologue，函数返回阶段也要执行一段代码，负责清理临时变量、销毁函数栈，这段代码就是 epilogue
    - 如果用户在函数返回位置添加断点，应该停在 epilogue 开始位置，因为此时局部变量仍可以访问
- isa，当前指令所属的指令集，通常指令集只有一个，但是 ARM 还有 thumb 指令集，可以和标准 arm 指令混合
- discriminator，当前指令属于哪个代码块，一个源文件可以有多个代码块

对我们来说，只有前几个状态需要关心，甚至 column 与 op_index 都不用管（不需要支持 VLIW），遇到不关心的指令，直接跳过即可。
