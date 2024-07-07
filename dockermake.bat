@ REM 如果要生成可引导硬盘镜像，需要加入参数 --privileged=true，允许在容器中使用 loop 设备
@ docker run -it --name inst --rm -v .:/mnt/wheel -w /mnt/wheel --privileged=true %* /bin/bash
