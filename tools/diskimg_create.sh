#! /bin/bash

# 创建虚拟磁盘并安装 grub，硬盘只有一个主分区
# 虚拟磁盘只需创建一次，创建需要 root 权限

# $1 目标磁盘镜像

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
disk_loop=$(sudo losetup --show -f $1)
part_loop=$(sudo losetup --show -f $1 -o 1M)

# 主分区格式化
sudo mkfs.vfat $part_loop

# 主分区文件系统挂载
mount_dir=$(mktemp -d)
sudo mount $part_loop $mount_dir
sudo mkdir -p $mount_dir/boot/grub

# 安装引导器
sudo grub-install \
    --no-floppy \
    --directory=$(dirname $0)/grub-i386-pc \
    --root-directory=$mount_dir \
    --modules="normal part_msdos ext2 multiboot" \
    $disk_loop

# 清理
sudo umount $mount_dir
rm -rf $mount_dir
sudo losetup -d $disk_loop
sudo losetup -d $part_loop
