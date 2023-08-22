#! /bin/bash

# 将文件拷贝到磁盘镜像中的指定路径

# $1 source file
# $2 disk image
# $3 target path

# mdel  -i $2@@1M ::/$3 2> /dev/null
mcopy -i $2@@1M -D o -nv $1 ::/$3
