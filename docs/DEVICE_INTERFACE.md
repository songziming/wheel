# 块设备管理

内核定义一套接口，由 arch 实现。

arch 向系统注册块设备驱动、块设备对象。系统其他模块可以使用统一的块设备接口读写。

## 竞争控制

每个块设备都有自旋锁，避免两个进程同时操作。

读写存储设备比较耗时，不适合自旋锁，最好换成信号量/互斥锁。某个任务调用块设备的 read/write 函数，自动进入 PENDING 状态。

阻塞控制应该由具体块设备的驱动负责，因为并不是所有驱动都需要阻塞（例如 PIO 就没有阻塞）。


## 专门的存储管理服务？

专门创建一个 task，负责执行所有的磁盘读写。其他任务通过消息队列与这个任务通信，发送命令。

这样任务模型会简单很多，对同一个块设备的并发读写操作可以自动序列化。而且消息队列自带阻塞功能。

磁盘服务应该由具体的 driver 实现，每检测到一个硬件，就启动一个新的 storage task。


## 对设备的引用

直接持有 blk_dev_t 指针，这样最直接，但设备必须一直存在。

如果是移动存储介质，可能随时被拔出，导致 blk_dev_t 对象释放，原来持有的指针变成野指针。

应该实现一套 handle 机制，双向链接，删除内核对象时，将所有引用自己的 handle 改为 INVALID_HANDLE。
要使用内核对象，就要使用 open 打开设备，使用完成用 close 关闭设备。
