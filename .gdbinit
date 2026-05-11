python
import os
import subprocess
import gdb

# 如果传入了程序路径（例如 gdb build/unit），跳过所有自动设置
if gdb.current_progspace().filename is not None:
    print(".gdbinit: program specified, skipping auto-setup")
else:

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

    # ---- qemu 命令 ---------------------------------------------------------

    class QemuCmd(gdb.Command):
        """Connect to QEMU and load kernel symbols."""

        def __init__(self):
            super().__init__("qemu", gdb.COMMAND_RUNNING)

        def invoke(self, arg, from_tty):
            gdb.execute("symbol-file build/wheel.elf")

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

    QemuCmd()

    # ---- $pcpu / $thiscpu --------------------------------------------------

    class PerCpuFunc(gdb.Function):
        """$pcpu(cpu_index, var_name) — read per-CPU variable for given CPU."""

        def __init__(self):
            super().__init__("pcpu")

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
        """$thiscpu(var_name) — read per-CPU variable for the current CPU."""

        def __init__(self):
            super().__init__("thiscpu")

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

    PerCpuFunc()
    ThisCpuFunc()

    # ---- 启动时自动连接 ----------------------------------------------------

    try:
        gdb.execute("qemu")
    except Exception as e:
        print("Auto-connect failed: %s" % e)
        print("Type 'qemu' to retry once QEMU is running.")
end
