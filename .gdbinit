python
import os
import subprocess
import gdb

def _is_wsl():
    try:
        with open("/proc/sys/kernel/osrelease") as f:
            r = f.read().lower()
            if "microsoft" in r or "wsl" in r:
                return True
    except Exception:
        pass
    return "WSL_DISTRO_NAME" in os.environ

def _wsl_host_ip():
    try:
        with open("/etc/resolv.conf") as f:
            for line in f:
                if line.startswith("nameserver"):
                    return line.split()[1]
    except Exception:
        pass
    try:
        out = subprocess.check_output(["ip", "route", "show", "default"], text=True)
        return out.split()[2]
    except Exception:
        pass
    return None

class QemuCmd(gdb.Command):
    '''qemu命令，用来连接虚拟机'''
    def __init__(self):
        super().__init__("qemu", gdb.COMMAND_RUNNING)
        print('adding command qemu')
    def invoke(self, arg, from_tty):
        if _is_wsl():
            host = _wsl_host_ip()
            if host:
                print("WSL: connecting to Windows host %s:4444" % host)
                gdb.execute("target remote %s:4444" % host)
            else:
                print("WSL: could not determine host IP, using localhost")
                gdb.execute("target remote localhost:4444")
        else:
            print("Connecting to localhost:4444")
            gdb.execute("target remote localhost:4444")
        gdb.execute("symbol-file build/wheel.elf")

class PerCpuFunc(gdb.Function):
    '''$pcpu(cpu, &var) 访问指定CPU的percpu变量，用法同 percpu_ptr(i, &var)'''
    def __init__(self):
        super().__init__("pcpu")
        print('adding command pcpu')
    def invoke(self, cpu, var_ptr):
        # var_ptr 是 &var 求值后的 gdb.Value，类型是 T*（指向变量的指针）
        # 从中提取模板地址和类型，计算percpu副本地址后解引用
        cpu_idx = int(cpu)
        base    = int(gdb.parse_and_eval("g_percpu_base"))
        step    = int(gdb.parse_and_eval("g_percpu_step"))
        if base == 0 or step == 0:
            raise gdb.GdbError("percpu not initialized yet")
        tpl_addr = int(var_ptr)
        actual   = (tpl_addr + base + step * cpu_idx) & 0xFFFFFFFFFFFFFFFF
        return gdb.Value(actual).cast(var_ptr.type).dereference()

class ThisCpuFunc(gdb.Function):
    '''$thiscpu(&var) 访问当前CPU的percpu变量，等价于 $pcpu(cpu_index(), &var)'''
    def __init__(self):
        super().__init__("thiscpu")
        print('adding command thiscpu')
    def invoke(self, var_ptr):
        # var_ptr 是 &var 求值后的 gdb.Value，类型是 T*
        # 用 gs_base 做偏移，和内核 thiscpu_ptr 一致
        gs_base  = int(gdb.parse_and_eval("$gs_base"))
        tpl_addr = int(var_ptr)
        actual   = (tpl_addr + gs_base) & 0xFFFFFFFFFFFFFFFF
        return gdb.Value(actual).cast(var_ptr.type).dereference()



if gdb.current_progspace().filename is not None:
    # 如果传入了程序路径（例如 gdb build/unit），跳过所有自动设置
    print(".gdbinit: program specified, skipping auto-setup")
else:
    # 注册辅助命令
    QemuCmd()
    PerCpuFunc()
    ThisCpuFunc()

    # 启动时尝试连接qemu
    try:
        gdb.execute("qemu")
    except Exception as e:
        print("Auto-connect failed: %s" % e)
        print("Type 'qemu' to retry once QEMU is running.")
end
