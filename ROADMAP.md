next to do

- [ ] 详细分析 rbtree、rbtree_aug，分析代码有无问题，在内核态运行单元测试
- [ ] PCIe 使用内存映射的读写函数



不那么重要的优化点：
- [ ] 调试输出区分不同 level，只有高于 DEBUG 的输出才显示在屏幕上
- [ ] 单元测试使用 ASSERT 检查，替换内核的 assert_fail 实现
- [ ] 改进单元测试，不必要求和 gtest 一样按 suite-case 命名，可以更加灵活
- [ ] 开发 gdb 调试内核用的辅助命令（使用 gdb python API）
- [ ] 通过 arch-header 实现一部分公共接口，例如 int_depth、in、out，不必在 arch_api 里面定义再由 arch 实现
      这样 arch-header 可以实现为内联函数，或者是宏定义，不需要编译器的 LTO 支持。
      我们对编译器的要求不应太高，避免使用 extension，才能移植到其他平台
