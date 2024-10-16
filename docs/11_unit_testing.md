# Wheel 采用的单元测试方案

内核代码编译成动态库，测试程序链接这个动态库，调用其中的函数进行测试，在 host 环境下运行。

但是要去掉内核中的汇编代码（包括一些内联），因为其中包括 32-bit 代码，链接在一起会产生 R_X86_64_32S reloc，导致无法生成动态库。
内核代码被链接到地址空间最高 2GB，且使用 mcmodel=kernel，所以才没问题，单元测试不满足这个条件。

还有一些测试代码需要去掉，例如 stack_chk_fail、ubsan、asan，因为这些测试可以由 host 提供，比自己实现的功能更完善。

除此之外，内核代码几乎不用修改。

使用动态库很容易 mock，测试程序定义的同名函数会自动覆盖动态库中的实现。

运行在 host 环境下，可以利用系统库，可以动态申请内存，可以统计代码覆盖率。

# 自己开发的单元测试框架

单元测试可以使用 googletest，因为只需要通过公共头文件调用内核函数。

~~~bash
sudo apt install libgtest-dev
~~~

代码使用 C++，类似的测试可以使用模板复用。

# 单元测试的目的和局限

无法测试多线程，无法测试多任务调度，无法压力测试，无法跑分统计性能。

单元测试只是第一步，更多测试仍要在 OS 内运行。

