# RISC-V assembly

## 题面

了解一点 RISC-V 汇编是很重要的，您在 6.004 中已经接触过。

在 xv6 仓库中有一个文件 `user/call.c`。`make fs.img` 对它进行编译，并在 `user/call.asm` 中生成可读的程序汇编版本。

请阅读 `call.asm` 中的函数 `g`、`f` 和 `main`。RISC-V 的使用手册参见[参考页面]([6.S081 / Fall 2020 (mit.edu)](https://pdos.csail.mit.edu/6.828/2020/reference.html)]。

下面是一些需要回答的问题（将答案存储在文件 `answers-traps.txt` 中）：

- 哪些寄存器包含函数的参数？例如，在 `main` 调用 `printf` 时，哪个寄存器保存了 13 ?

- 在 `main` 的汇编代码中，对函数 `f` 的调用在哪里？`g` 在哪里？（提示：编译器可以内联函数）

- 函数 `printf` 位于什么地址？

- 在 `main` 中的 `jalr` 到 `printf` 之后，`ra` 寄存器中的值是什么？

- 运行下面的代码。

  ```c
  unsigned int i = 0x00646c72;
  printf("H%x Wo%s", 57616, &i);
  ```

  输出是什么？[这是一个 ASCII 表](http://web.cs.mun.ca/~michael/c/ascii-table.html)，它将字节映射到字符。

  输出结果取决于 RISC-V 是小端序的。如果 RISC-V 是大端序的，你会将 `i` 设置为什么以产生相同的输出？你是否需要将 `57616` 更改为其他值？

  [这是对 little- endian 和 big-endian 的描述](http://www.webopedia.com/TERM/b/big_endian.html) 和 [一个更古怪的描述](http://www.networksorcery.com/enp/ien/ien137.txt)。

- 在下面的代码中，`'y='` 后面会打印什么？（注意：答案不是一个特定的值）为什么会这样？

  ```c
  printf("x=%d y=%d", 3);
  ```