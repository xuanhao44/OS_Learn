# chapter2 - 操作系统的组织结构

## 2.0 Operating system organization

对操作系统的一个重要的要求就是支持同时进行多个活动（进程）。

- 操作系统必须让各个进程分时共享计算资源。`time_share`
  - eg：进程数 > CPU 数，那么操作系统就需要保证每个进程都有机会执行。
- 操作系统必须处理好进程间的隔离。`arrange for isolation`
  - eg：比如没有相互关联的进程 A 和 B，若进程 A 出错，至少不能影响到与之无关的进程 B 的运行。
- 当然，也不能完全隔离 `Complete isolation` 而没有任何交流，毕竟进程间也有通信 `intentionally interact`。
  - eg：管道 `pipelines`

所以一个操作系统需要满足三个要求：复用（物理资源轮流共享），隔离（进程隔离），互动（进程通信）。`multiplexing, isolation, and interaction`

这第二章就是从整体上讲操作系统是怎样被编写以达到这三个要求的。同时我们关注的是宏内核 `monolithic kernel`，一种被大多数 Unix 操作系统所使用的设计。

这章同时也从整体上讲述了 xv6 进程——它是 xv6 系统中的一个隔离单元。还有当 xv6 启动时创建的第一个进程。