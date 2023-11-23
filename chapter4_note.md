# MIT 6.S081 - chapter 4 note

## 0 起始

涉及的课程包含 5 - 8 节。

- [riscv-spec.pdf (mit.edu)](https://pdos.csail.mit.edu/6.828/2020/readings/riscv-calling.pdf)

- [Lec05 Calling conventions and stack frames RISC-V (TA) - MIT6.S081 (gitbook.io)](https://mit-public-courses-cn-translatio.gitbook.io/mit6-s081/lec05-calling-conventions-and-stack-frames-risc-v)
- [Lec06 Isolation & system call entry/exit (Robert) - MIT6.S081 (gitbook.io)](https://mit-public-courses-cn-translatio.gitbook.io/mit6-s081/lec06-isolation-and-system-call-entry-exit-robert)
- [Lec08 Page faults (Frans) - MIT6.S081 (gitbook.io)](https://mit-public-courses-cn-translatio.gitbook.io/mit6-s081/lec08-page-faults-frans)

## 1 回忆计组

简单列一下重点。

### 1.1 caller 和 callee

- Caller Saved 寄存器在函数调用的时候不会保存
- Callee Saved 寄存器在函数调用的时候会保存

基本上来说：

- 任何一个 Caller Saved 寄存器，作为调用方的函数要小心可能的数据可能的变化；

- 任何一个 Callee Saved 寄存器，作为被调用方的函数要小心寄存器的值不会相应的变化。

### 1.2 stackframe

<img src="https://typora-1304621073.cos.ap-guangzhou.myqcloud.com/typora/stackframe.png" alt="stackframe" style="zoom: 33%;" />

1. 对于 Stack 来说，是从高地址开始向低地址使用，所以栈总是向下增长。当我们想要创建一个新的 Stack Frame 的时候，总是对当前的 Stack Pointer 做减法。
2. SP（Stack Pointer）指向 Stack 的底部并代表了当前 Stack Frame 的位置，FP（Frame Pointer）指向当前 Stack Frame 的顶部。因为 Return address 和指向前一个 Stack Frame 的的指针都在当前 Stack Frame 的固定位置，所以可以通过当前的 FP 寄存器寻址到这两个数据。

## 2 traps 基本机制

CPU 暂停当前指令流执行，跳转到一段特定代码去处理特殊事件的情况，我们称为**陷阱 trap**。

CPU 通常有三种**特殊事件**会暂停当前指令流的执行，并**跳转到一段特定代码**来处理这些事件。

- **系统调用**，执行 RISC-V 的 **ecall** 指令时。
- **异常**，用户或内核指令可能进行了一些非法操作，例如除以 0 或者使用无效的虚拟地址时。
- **设备中断**，当一个设备因某些原因（如磁盘的读/写工作完成）需要 CPU 及时处理时。

以在用户空间下发生 trap 为例：

- trap 会强制地从用户空间切换到内核空间。
- 在内核空间下，内核保存一些寄存器和其它状态，以便稍后恢复执行被暂停的进程指令流。
- 然后内核就开始执行一段特定的处理代码（如系统调用函数、设备驱动程序等）。
- 处理完毕后，内核就恢复之前保存的寄存器和其它进程状态；
- 最后从内核空间返回到用户空间，恢复执行之前被暂停的用户指令流。

trap 最好是对用户进程**透明**的——什么是透明（*transparent*）？和我们平时的理解不太一样：

保存和恢复相关寄存器应该由内核负责而不让用户来操心，从 trap 返回时也应该回到进程指令流被中止的位置，用户进程应该感受不到 trap 的发生。

也就是说—— xv6 的内核处理所有的 trap。

*不在这里提前介绍 Traps From User Space 的内容，关于 RISC-V 的部分也放在 Traps From User Space 里讲。*

## 3 Traps From User Space

### 3.1 整体流程

> 参考：Foreword、[6.2 Trap 代码执行流程 - MIT6.S081 (gitbook.io)](https://mit-public-courses-cn-translatio.gitbook.io/mit6-s081/lec06-isolation-and-system-call-entry-exit-robert/6.2-trap-dai-ma-zhi-xing-liu-cheng)

（函数）：uservec -> usertrap -> usertrapret -> userret

- **uservec**：位于 trampoline 的前半部分汇编代码，用于做一些陷入内核之前的准备工作，例如保存用户空间下的一系列寄存器，加载内核栈、内核页表等设置，然后跳转到 usertrap。这一部分，我们称为 **trap vector**。
- **usertrap**：位于内核中的一段 C 代码（trap.c），判断引起 trap 的事件类型，并决定如何处理该 trap，如跳转到系统调用函数（syscall）、设备驱动程序等。我们一般也称其为 **trap handler**。
- **usertrapret**：位于内核中的另一段 C 代码，trap 被处理完之后，就会跳转到 usertrapret，保存内核栈、内核页表等内核相关信息，进行一些设置，然后跳转到 userret。
- **userret**：位于 trampoline 的后半部分汇编代码，用于做一些返回用户空间的恢复工作，恢复之前保存的用户空间寄存器，最后返回用户空间，恢复用户进程指令流的执行。
