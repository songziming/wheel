symbol-file build/wheel.elf
target remote localhost:4444

b type_mismatch_common
b handle_assert_fail
