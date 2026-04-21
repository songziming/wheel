# 代码质量保证

我们采取多种手段保证内核代码质量

### 单元测试和代码覆盖率
详见：03_unit_test.md

### stack-protector
编译器生成额外代码，在函数退出时检查 stack-frame 是否完好。
会带来额外开销，只在 debug 模式下开启。

### control-flow-integration（-fsanitize=cli）
必须结合 -flto。Android 也开启了这个选项。
防护函数指针、虚表的攻击。Android 大量使用 C++，普遍存在虚表，因此 CFI 有必要。
CFI 可以检查目标函数的类型和函数指针的类型是否一致，防止指针指向错误的目标函数。
wheel 没有用到 C++，函数指针用得也不多，CFI 作用不大。

### UBSAN（-fsanitize=undefined）
检查 C 语言未定义行为，保证代码规范。
需要我们提供异常处理函数。
UB 类型很多，每种 UB 都需要专属的 ub-handler func。

### ASAN（-fsanitize=address）
主要检查动态内存分配，需要划分出一段内存，占总体内存的 1/8，用来记录每个字节是否可用。
主要用在用户态，内核则比较复杂，需要 kernel 提供支持。
内核态的保护叫做 kasan
