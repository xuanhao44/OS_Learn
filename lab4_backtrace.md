# MIT 6.S081 - Lab Traps - Backtrace

## 题面

在调试时，回溯通常很有用：在错误发生点以上的堆栈上的函数调用列表。

在 `kernel/printf.c` 中实现一个 `backtrace()` 函数。在 `sys_sleep` 中插入对该函数的调用，然后运行 `bttest`，它会调用 `sys_sleep`。输出应该如下所示：

```shell
backtrace:
0x0000000080002cda
0x0000000080002bb6
0x0000000080002898
```

之后退出 qemu。在您的终端中：地址可能略有不同，但如果您运行 `addr2line -e kernel/kernel`（或 `riscv64-unknown-elf-addr2line -e kernel/kernel`）并按如下方式剪切和粘贴上述地址：

```shell
$ addr2line -e kernel/kernel
0x0000000080002de2
0x0000000080002f4a
0x0000000080002bfc
Ctrl-D
```

你应该会看到类似下面的内容：

```shell
kernel/sysproc.c:74
kernel/syscall.c:224
kernel/trap.c:85
```

编译器在每个栈帧中放入一个帧指针，其中保存调用者的帧指针的地址。你的回溯应该使用这些帧指针向上遍历栈，并打印出保存在每个栈帧中的返回地址。

一些提示：

1. 将 `backtrace` 的原型添加到 `kernel/defs.h`，以便大家可以在 `sys_sleep` 中调用 `backtrace`。

2. GCC 编译器将当前执行函数的帧指针存储在寄存器 s0 中。将以下函数添加到 `kernel/riscv.h`：

  ```c
  static inline uint64
  r_fp()
  {
    uint64 x;
    asm volatile("mv %0, s0" : "=r" (x) );
    return x;
  }
  ```

  并在 `backtrace` 中调用此函数来读取当前帧指针。该函数使用 [内联汇编](https://gcc.gnu.org/onlinedocs/gcc/Using-Assembly-Language-with-C.html) 来读取 `s0`。

3. 这些 [课堂讲稿](https://pdos.csail.mit.edu/6.828/2020/lec/l-riscv-slides.pdf) 对堆栈框架的布局有一个了解。请注意，返回地址与 stackframe 的帧指针的偏移量是固定的（-8），而保存的帧指针与帧指针的偏移量是固定的（-16）。

4. xv6 为 xv6 内核中的每个栈分配一页，地址与页对齐。可以使用 `PGROUNDDOWN(fp)` 和 `PGROUNDUP(fp)` 计算栈页的顶部和底部地址（参见 `kernel/riscv.h`）。这些数字有助于 `backtrace` 结束其循环。

5. 一旦回溯工作正常，在 `kernel/printf.c` 中的 `panic` 调用它，这样您就可以在内核错误时看到它的回溯。

## `backtrace`

按照提示一步步做，难度出在 `backtrace` 这一部分，而写好这一部分就要理解 stackframe。

要是还不会的要去看教案：[5.5 Stack - MIT6.S081 (gitbook.io)](https://mit-public-courses-cn-translatio.gitbook.io/mit6-s081/lec05-calling-conventions-and-stack-frames-risc-v/5.5-stack)

<img src="https://typora-1304621073.cos.ap-guangzhou.myqcloud.com/typora/stackframe.png" alt="stackframe" style="zoom: 33%;" />

其实就是用 fp 一路向上爬到顶端，如何判断爬到顶端没有—— 和 `PGROUNDUP(fp)` 比较。

当前函数的 ra 位于 fp - 8 的位置，前一个函数的 fp 位于 fp - 16 的位置。

```c
void backtrace(void)
{
  printf("backtrace:\n");
  uint64 fp = r_fp();
  uint64 top = PGROUNDUP(fp); // 顶端
  while (fp < top)
  {
    uint64 ra = *(uint64 *)(fp - 8); // 当前函数的 ra 位于 fp - 8 的位置
    fp = *(uint64 *)(fp - 16);       // 前一个函数的 fp 位于 fp - 16 的位置
    printf("%p\n", ra);              // 要打印的是 ra
  }
}
```

## 验证

*注意添加的语句是上面 backtrace 的结果，不是从指导书上复制过来的。*

<img src="https://typora-1304621073.cos.ap-guangzhou.myqcloud.com/typora/lab4_backtrace.png" alt="backtrace 验证" style="zoom: 67%;" />
