## debug using Qemu and gdb

Launch qemu with `./run_iso.sh`.

And run gdb in another terminal (requires newer version):

```bash
(gdb) target remote localhost:4444
(gdb) symbol-file build/wheel.elf
(gdb) b handle_assert_fail
```

## debug using Bochs

Extract symbol list from kernel image:

```bash
nm build/wheel.elf | awk '{ print $1" "$3 }' > build/wheel.sym
```

In bochs, use the following command to load symbol file:

```
ldsym global "build/wheel.sym"
```
