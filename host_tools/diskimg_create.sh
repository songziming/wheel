#! /bin/bash

# 创建虚拟磁盘并安装 grub，硬盘只有一个主分区
# 虚拟磁盘只需创建一次，创建需要 root 权限

# $1 目标磁盘镜像

SUDO=

if [ -f "$1" ]; then
    exit 0
fi

# 创建虚拟磁盘，共 16MB
dd if=/dev/zero of=$1 bs=512 count=32768

# 创建分区表，主分区从 1M 开始
(
    echo    n       # new partition
    echo    p       # primary
    echo    1       # partition number
    echo    2048    # first sector, skip firt 1M
    echo    32767   # last sector
    echo    a       # bootable
    echo    w       # write
) | fdisk $1

# 创建两个loop文件，分别表示整块磁盘和主分区
disk_loop=$($SUDO losetup --show -f $1)
part_loop=$($SUDO losetup --show -f $1 -o 1M)

# 主分区格式化
$SUDO mkfs.vfat $part_loop

# 主分区文件系统挂载
mount_dir=$(mktemp -d)
$SUDO mount $part_loop $mount_dir
$SUDO mkdir -p $mount_dir/boot/grub

# 写入 grub 配置文件
cp $(dirname $0)/grub.cfg $mount_dir/boot/grub

# TODO 将内核文件 wheel.bin 也复制进来

# 安装引导器
$SUDO grub-install \
    --no-floppy \
    --root-directory=$mount_dir \
    --locale-directory=/usr/share/locale \
    --modules="normal part_msdos ext2 multiboot" \
    $disk_loop

# --directory=$(dirname $0)/grub-i386-pc \

# 清理
$SUDO umount $mount_dir
rm -rf $mount_dir
$SUDO losetup -d $disk_loop
$SUDO losetup -d $part_loop
