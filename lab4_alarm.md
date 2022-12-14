# MIT 6.S081 - Lab Traps - Alarm

## 题面

### 引入

在这个练习中，您将为 xv6 添加一个特性，该特性会在进程使用 CPU 时间时周期性地发出警报（*periodically alerts*）。这对于计算密集型（*compute-bound*）的进程想要限制它们占用的 CPU 时间，或者想要进行计算但又想采取一些周期性操作的进程可能很有用。

更一般地说，你将实现用户级中断/错误处理程序的基本形式；例如，您可以使用类似的东西来处理应用程序中的缺页异常（*page faults*）。

如果您的解决方案通过了 `alarmtest` 和 `usertests`，那么它就是正确的。

### 说明

您应该添加一个新的 `sigalarm(interval, handler)` 系统调用。如果应用程序调用了`sigalarm(n, fn)`，那么在该程序消耗的 CPU 时间的每 n 个 “tick” 之后，内核就应该调用应用程序函数 `fn`。当 `fn` 返回时，应用程序应该从中断的位置恢复。

在 xv6 中，时钟是一个相当随意（*arbitrary*）的时间单位，由硬件定时器产生中断的频率决定。如果应用程序调用 `sigalarm(0,0)`，内核应该停止产生周期性的报警调用。

你会在 xv6 仓库中找到文件 `user/alarmtest.c`。将其添加到 `Makefile` 中。除非已经添加了 `sigalarm` 和 `sigreturn` 系统调用（见下文），否则它将无法正确编译。

`Alarmtest` 调用 test0 中的 `sigalarm(2, periodic)`，要求内核强制每 2 个时钟周期调用 `periodic()` 一次，之后自旋（*spin*）一段时间。你可以在 `user/alarmtest.asm` 中看到编译测试的 `alarmtest` 的汇编代码。

当 `alarmtest` 产生类似这样的输出并且 `usertests` 也正确运行时，你的解决方案是正确的：

```shell
$ alarmtest
test0 start
........alarm!
test0 passed
test1 start
...alarm!
..alarm!
...alarm!
..alarm!
...alarm!
..alarm!
...alarm!
..alarm!
...alarm!
..alarm!
test1 passed
test2 start
................alarm!
test2 passed
$ usertests
...
ALL TESTS PASSED
$
```

当你完成时，你的解决方案将只有几行代码，但可能很难把它做好。

我们将使用原仓库中的 `alarmtest.c` 版本来测试代码。你可以修改 `alarmtest.c` 以帮助调试，但要确保在原来的 `alarmtest` 下所有测试都通过了。

### Test0：调用处理程序

首先修改内核，使其跳转到用户空间的警报处理程序，这将导致 test0 打印 "alarm!"。

不用担心输出 “alarm!” 之后会发生什么；如果你的程序在打印 "alarm!" 后崩溃了，也没有问题。

这里有一些提示：

- 你需要修改 `Makefile`，将 `alarmtest.c` 编译为 xv6 用户程序。

- 应该放在 `user/user.h` 中的正确声明是：

	```c
	int sigalarm(int ticks, void (*handler)());
	int sigreturn(void);
	```

- 更新 `user/usys.pl` (它生成 `user/usys.S`)、`kernel/syscall.h` 和 `kernel/syscall.c`，以允许 `alarmtest` 调用 `sigalarm` 和 `sigreturn` 系统调用。

- 现在，你的 `sys_sigreturn` 应该只返回 0。

- 你的 `sys_sigalarm()` 应该将警报间隔（`alarm interval`）和指向处理程序函数的指针存储在 `proc` 结构体的新字段中（在 `kernel/proc.h` 中）。

- 你需要跟踪从上一次调用（或直到下一次调用）进程的警报处理程序以来已经传递了多少个时钟周期；为此你也需要在 `struct proc` 中添加一个新字段。你可以在 `proc.c` 的 `allocproc()` 中初始化 `proc` 字段。

- 每个时钟周期，硬件时钟强制执行一个中断（*forces an interrupt*），由 `kernel/trap.c` 中的 `usertrap()` 处理。

- 只有在出现定时器中断（*timer interrupt*）的情况下，你才需要操作进程的警报时钟；你想要的是：

  ```c
  if(which_dev == 2) ...
  ```

- 只有在进程有定时器未完成（*timer outstanding*）时，才调用报警函数。请注意，用户的报警函数的地址可能是 0（例如在 `user/alarmtest.asm` 中，`periodic` 位于地址 0）。

- 你需要修改 `usertrap()`，以便在进程的警报间隔过期时，用户进程执行处理程序函数。当 RISC-V 上陷阱返回到用户空间时，什么决定用户空间代码恢复执行的指令地址?

- 让 qemu 只使用一个 CPU 的话，使用 gdb 时查看陷阱会更容易，你可以这样做：

  ```c
  make CPUS=1 qemu-gdb
  ```

- 如果 `alarmtest` 打印 "alarm!" 就成功了。

### Test1/test2()：恢复中断的代码

很有可能发生的事情：`alarmtest` 在 test0 或 test1 中打印 “alarm!” 后崩溃，或者 `alarmtest` (最终代码) 打印 “test1 failed”，或者 `alarmtest` 退出时没有打印 “test1 passed” 。

要解决这些问题，你必须确保在警报处理程序完成时，控制返回到用户程序最初被定时器中断时的指令。你必须确保寄存器内容恢复到中断时的值，以便用户程序在报警后可以继续不受干扰。最后，应该在每次报警计数器发出警报后 “重新武装”（*re-arm*?） 它，以便定期调用处理程序。

作为一个起点，我们已经为您做出了一个设计决策：用户警报处理程序在完成时需要调用 `sigreturn` 系统调用。请查看 `alarmtest.c` 中的 `periodic` 作为示例。这意味着，可以向 `usertrap` 和 `sys_sigreturn` 添加代码，它们协同工作，使用户进程在处理完警报后正常恢复。

一些提示：

- 您的解决方案将要求您保存和恢复寄存器——您需要保存和恢复哪些寄存器才能正确地恢复中断的代码？（提示：会有很多）。
- 在定时器结束时，让 `usertrap` 在 `struct proc` 中保存足够的状态，使 `sigreturn` 能够正确地返回到被中断的用户代码。
- 防止对处理程序的可重入调用——如果处理程序尚未返回，内核不应再次调用它。test2 对此进行测试。

一旦通过了 test0、test1 和 test2 就运行 usertest，以确保没有破坏内核的任何其他部分。