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
    '''pcpu命令，用来访问percpu-var'''
    def __init__(self):
        super().__init__("pcpu")
        print('adding command pcpu')
    def invoke(self, cpu, var_name):
        cpu_idx = int(cpu)
        name    = str(var_name)
        val     = gdb.parse_and_eval(name)
        base    = int(gdb.parse_and_eval("g_percpu_base"))
        step    = int(gdb.parse_and_eval("g_percpu_step"))
        if base == 0 or step == 0:
            raise gdb.GdbError("percpu not initialized yet")
        tpl_addr = int(val.address)
        actual   = tpl_addr + base + step * cpu_idx
        return gdb.Value(actual).cast(val.type.pointer()).dereference()

class ThisCpuFunc(gdb.Function):
    '''thiscpu命令，用来访问当前线程的percpu-var'''
    def __init__(self):
        super().__init__("thiscpu")
        print('adding command pcpu')
    def invoke(self, var_name):
        cpu_idx = int(gdb.parse_and_eval("$pcpu(0, g_thiscpu_idx)"))
        name    = str(var_name)
        val     = gdb.parse_and_eval(name)
        base    = int(gdb.parse_and_eval("g_percpu_base"))
        step    = int(gdb.parse_and_eval("g_percpu_step"))
        if base == 0 or step == 0:
            raise gdb.GdbError("percpu not initialized yet")
        tpl_addr = int(val.address)
        actual   = tpl_addr + base + step * cpu_idx
        return gdb.Value(actual).cast(val.type.pointer()).dereference()



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
