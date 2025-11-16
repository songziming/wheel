在真实环境下运行，驱动是大问题
很多新硬件的驱动细节是闭源的，我们无法自己开发
所以使用各虚拟机提供的虚拟设备，是个人 osdev 的合理选择

VMware 虚拟设备列表
https://techdocs.broadcom.com/us/en/vmware-cis/vsphere/tools/12-4-0/vmware-tools-administration-12-4-0/introduction-to-vmware-tools/vmware-tools-device-drivers.html

然而 vmware 没有公开虚拟设备接口，而是自己实现 vmware tools，也就是自己给 guest os 开发了驱动。
不过 vmware svga 有开源参考代码。
https://github.com/prepare/vmware-svga

虽然是虚拟设备，仍需要 PCIe 子系统。
