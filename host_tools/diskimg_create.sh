#! /bin/bash

# 创建虚拟磁盘并安装 grub，硬盘只有一个主分区，起始偏移 1M
# $1 目标磁盘镜像
# $2 内核镜像

if [ "$EUID" -ne 0 ]
    then echo "run this script as root"
    exit
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
disk_loop=$(losetup --show -f $1)
part_loop=$(losetup --show -f $1 -o 1M)

# 主分区格式化
mkfs.vfat $part_loop

# 主分区文件系统挂载
mount_dir=$(mktemp -d)
mount $part_loop $mount_dir

# 安装引导器（BIOS 版本）
grub-install \
    --target=i386-pc \
    --no-floppy \
    --root-directory=$mount_dir \
    --modules="normal part_msdos ext2 multiboot" \
    $disk_loop

# TODO 创建 uefi 版本的引导器，就是一个普通文件，拷贝到 FAT32 分区即可

# 清理
umount $mount_dir
rm -rf $mount_dir
losetup -d $disk_loop
losetup -d $part_loop
