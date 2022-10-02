# MIT 6.S081 - Lab System calls -- overview

实验二前提条件：

- xv6 book chapter 2，chapter 4：4.3 & 4.4
- 课程 lec03：[Lec03 OS Organization and System Calls (Frans) - MIT6.S081 (gitbook.io)](https://mit-public-courses-cn-translatio.gitbook.io/mit6-s081/lec03-os-organization-and-system-calls)
- 部分 xv6 源码

主题：

完成两个系统调用 trace 和 sysinfo。

这次实验更多的是概念上的理解，实际的实验内容和细节没有实验一多；但是概念确实有点多，上手没有实验一快。

怎么做：

xv6 book、课程翻译、xv6 源码、学校的指导书、网上的 GDB 调试知识，轮番着反复看。知识了解到一定程度就可以去写实验了，不必了解的过于透彻；在写实验的时候进一步接触源码，照葫芦画瓢的写完之后必然有新的发现和问题，这个时候再带着问题去读之前没看懂的资料。

这次学校的指导书意外的帮到我完成实验了，没有指导书的话，我可能在了解了众多知识之后像个没头苍蝇一样不知道从何处下手完成实验。同时报告中的问题也是在有意的指导你去看关键的部分。

## 结束：报告

一、回答问题

1. 阅读 `kernel/syscall.c`，试解释函数 `syscall()` 如何根据系统调用号调用对应的系统调用处理函数（例如 `sys_fork`）？`syscall()` 将具体系统调用的返回值存放在哪里？

2. 阅读 `kernel/syscall.c`，哪些函数用于传递系统调用参数？试解释 `argraw()` 函数的含义。

3. 阅读 `kernel/proc.c` 和 `proc.h`，进程控制块存储在哪个数组中？进程控制块中哪个成员指示了进程的状态？一共有哪些状态？

4. 阅读 `kernel/kalloc.c`，哪个结构体中的哪个成员可以指示空闲的内存页？xv6 中的一个页有多少字节？

5. 阅读 `kernel/vm.c`，试解释 `copyout()` 函数各个参数的含义。

二、实验详细设计



三、实验结果截图
