# MIT 6.S081 - Lab Utilities - xargs

## 0 理解题意

老实说 xargs 本身确实有点难懂，然后实验里对它的描述也是一团浆糊。

例：（linux 上执行）

```shell
$ echo 4321 8765 | xargs echo 1234 5678
1234 5678 4321 8765
```

xargs 后面才是需要执行的程序，它后面已经接上了两个参数 1234 和 5678；而 xargs 的作用是把管道左边的程序的输出也作为右边的参数。比如在这里就等同于：

```shell
$ echo 1234 5678 4321 8765
1234 5678 4321 8765
```

> 实验中你不必考虑管道的实现与使用，你的程序可以直接从标准输入中读取管道传递给你的字符串。

这句话的意思是我们需要处理的东西只有如 `xargs echo 1234 5678`，然后 xargs 补充的参数是从标准输入 0 来的，也就是说我们只需要从标准输入中读取字符串然后处理即可。

其实这很合理，因为这就是使用管道，把标准输入重定向了。我认为过程类似于：

```c
#include "kernel/types.h"
#include "user/user.h"

// 等效于 echo hello world | wc

int main()
{
    int p[2];
    pipe(p);

    char *argv[] = {"wc", 0};

    if (fork() == 0)
    {
        // 子进程
        close(0);
        // 先关闭标准输入 stdin, 0 空出

        dup(p[0]);
        // 那么将管道的读端口拷贝在描述符 0 上

        close(p[0]);
        close(p[1]);
        // 关闭 p 中的描述符

        exec("wc", argv);
        // 执行 wc, 计算字数
    }
    else
    {
        // 父进程
        close(p[0]);
        // 关闭读端

        write(p[1], "hello world\n", 12);

        close(p[1]);
        // 写入完成，关闭写端
        wait(0);
    }

    exit(0);
}
```

## 1 处理参数

例 1：

```c
#include "kernel/types.h"
#include "user/user.h"
#include "kernel/param.h"

int main(int argc, char *argv[])
{
    /*
    argv[0] = "xargs"
    argv[1] = command
    argv[2] = para
    ...
    argv[END] = 0
    */

    for (int i = 0; i < argc; i++)
        printf("i = %d, %s\n", i, argv[i]);

    // 最后一个是 null，也可写作 0
    printf("END, but not in argv: i = %d, %s\n", argc, argv[argc]);

    // 取出参数无法用指针操作。因为不连续
    // 只能另外定义了

    char *argv_alter[MAXARG];
    int argc_alter = argc - 1;

    for (int i = 0; i < argc_alter; i++)
        argv_alter[i] = argv[i + 1];

    for (int i = 0; i < argc_alter; i++)
        printf("i = %d, %s\n", i, argv_alter[i]);

    exit(0);
}
```

样例输入输出：

```shell
$ xargs echo 1234 5678
i = 0, xargs
i = 1, echo
i = 2, 1234
i = 3, 5678
END, but not in argv: i = 4, (null)
i = 0, echo
i = 1, 1234
i = 2, 5678
```

例 2：

``` c
#include "kernel/types.h"
#include "user/user.h"
#include "kernel/param.h"

int main(int argc, char *argv[])
{
    /*
    argv[0] = "xargs"
    argv[1] = command
    argv[2] = para
    ...
    argv[END] = 0
    */
  
    char *argv_alter[MAXARG];
    int argc_alter = argc - 1;

    for (int i = 0; i < argc_alter; i++)
        argv_alter[i] = argv[i + 1];

    exec(argv_alter[0], argv_alter);

    exit(0);
}
```

样例输入输出：

```shell
$ xargs echo 1234 5678
1234 5678
```

## 2 实现

先把全部的代码实现放出来。程序并非一蹴而就而是多次迭代和纠错后得到的。

```c
#include "kernel/types.h"
#include "user/user.h"
#include "kernel/param.h"

// ulib.c 的 gets 改造而来
char *ugets(char *buf, int max)
{
    int i, cc;
    char c;

    for (i = 0; i + 1 < max;)
    {
        cc = read(0, &c, 1);
        if (cc < 1)
            break;
        // 在遇到 \n \r 和 blank 时停止循环
        if (c == '\n' || c == '\r' || c == ' ')
            break;
        // 不把 \n \r 和 blank 读到字符串中
        buf[i++] = c;
    }
    buf[i] = '\0';
    return buf;
}

// sh.c 的 getcmd 改造而来
int ugetcmd(char *buf, int nbuf)
{
    memset(buf, 0, nbuf);
    ugets(buf, nbuf);
    if (buf[0] == 0) // EOF
        return -1;
    return 0;
}

int main(int argc, char *argv[])
{
    /*
    argv[0] = "xargs"
    argv[1] = command
    argv[2] = para
    ...
    argv[END] = 0
    */

    if (fork() == 0)
    {
        // 还是在子进程里 exec 吧
        /*
        获得新参数列表
        之后就使用 exec(argv_alter[0], argv_alter);
        */
        char *argv_alter[MAXARG];
        int argc_alter = argc - 1;

        for (int i = 0; i < argc_alter; i++)
            argv_alter[i] = argv[i + 1];

        char buf[100];

        while (ugetcmd(buf, sizeof(buf)) >= 0)
        {
            // buf 一直在变，不能添加到字符串指针数组里
            // 故使用临时的 temp
            char temp[100];
            strcpy(temp, buf);
            argv_alter[argc_alter] = temp;
            argc_alter++;
        }
        argv_alter[argc_alter] = 0;

        // exec 执行
        exec(argv_alter[0], argv_alter);
        // 失败了就退出了
        printf("exec failed!\n");
        exit(1);
    }
    else
    {
        wait(0);
    }
    exit(0);
}
```

### 2.1 个人测试

实验框架提供的测试是 `./grade-lab-util xargs`，在自己写实验的时候不算很直观，那么该如何处理呢？

答案是手动实现一个管道重定向的功能。父进程向管道里传 `4321 8765`，然后子进程的输入从标准输入重定向为管道的读出端，从而达到类似于从标准输入读取到字符串的效果。

```c
#include "kernel/types.h"
#include "user/user.h"
#include "kernel/param.h"

// ulib.c 的 gets 改造而来
char *ugets(char *buf, int max)
{
    int i, cc;
    char c;

    for (i = 0; i + 1 < max;)
    {
        cc = read(0, &c, 1);
        if (cc < 1)
            break;
        // 在遇到 \n \r 和 blank 时停止循环
        if (c == '\n' || c == '\r' || c == ' ')
            break;
        // 不把 \n \r 和 blank 读到字符串中
        buf[i++] = c;
    }
    buf[i] = '\0';
    return buf;
}

// sh.c 的 getcmd 改造而来
int ugetcmd(char *buf, int nbuf)
{
    memset(buf, 0, nbuf);
    ugets(buf, nbuf);
    if (buf[0] == 0) // EOF
        return -1;
    return 0;
}

int main(int argc, char *argv[])
{
    /*
    argv[0] = "xargs"
    argv[1] = command
    argv[2] = para
    ...
    argv[END] = 0
    */

    int p[2];
    pipe(p);

    if (fork() == 0)
    {
        // 子进程
        close(0);
        // 先关闭标准输入 stdin, 0 空出

        dup(p[0]);
        // 那么将管道的读端口拷贝在描述符 0 上

        close(p[0]);
        close(p[1]);
        // 关闭 p 中的描述符

        char *argv_alter[MAXARG];
        int argc_alter = argc - 1;

        for (int i = 0; i < argc_alter; i++)
            argv_alter[i] = argv[i + 1];

        char buf[100];

        while (ugetcmd(buf, sizeof(buf)) >= 0)
        {
            // buf 一直在变，不能添加到字符串指针数组里
            // 故使用临时的 temp
            char temp[100];
            strcpy(temp, buf);
            argv_alter[argc_alter] = temp;
            argc_alter++;
        }
        argv_alter[argc_alter] = 0;

        exec(argv_alter[0], argv_alter);
        // 失败了就退出了
        printf("exec failed!\n");
        exit(1);
    }
    else
    {
        // 父进程
        close(p[0]);
        // 关闭读端

        write(p[1], "4321 8765", 9);

        close(p[1]);
        // 写入完成，关闭写端
        wait(0);
    }

    exit(0);
}
```

### 2.2 框架测试

框架测试为：

```shell
./grade-lab-util xargs
```

事实上，这个测试脚本只是在 xv6 的 sh 中执行了：

```sh
sh < xargstest.sh
```

查看 `xargstest.sh`：

```sh
mkdir a
echo hello > a/b
mkdir c
echo hello > c/b
echo hello > b
find . b | xargs grep hello
```

理论的输出应为：

```sh
$ $ $ $ $ $ hello
hello
hello
$ $
```

这个 sh 等效于：

```sh
$ mkdir a
$ echo hello > a/b
$ mkdir c
$ echo hello > c/b
$ echo hello > b
$ find . b | xargs grep hello
hello
hello
hello
```

所以也可以用这样的方式测试。

### 2.3 ulib.c 的 gets 和 sh.c 的 getcmd

先看 `sh.c`：（一部分）

```c
int getcmd(char *buf, int nbuf)
{
  fprintf(2, "$ ");
  memset(buf, 0, nbuf);
  gets(buf, nbuf);
  if (buf[0] == 0) // EOF
    return -1;
  return 0;
}

int main(void)
{
  static char buf[100];
  int fd;

  // Ensure that three file descriptors are open.
  while ((fd = open("console", O_RDWR)) >= 0)
  {
    if (fd >= 3)
    {
      close(fd);
      break;
    }
  }

  // Read and run input commands.
  while (getcmd(buf, sizeof(buf)) >= 0)
  {
    if (buf[0] == 'c' && buf[1] == 'd' && buf[2] == ' ')
    {
      // Chdir must be called by the parent, not the child.
      buf[strlen(buf) - 1] = 0; // chop \n
      if (chdir(buf + 3) < 0)
        fprintf(2, "cannot cd %s\n", buf + 3);
      continue;
    }
    if (fork1() == 0)
      runcmd(parsecmd(buf));
    wait(0);
  }
  exit(0);
}
```

再看 `ulib.c` 的 gets：

```c
char *gets(char *buf, int max)
{
  int i, cc;
  char c;

  for (i = 0; i + 1 < max;)
  {
    cc = read(0, &c, 1);
    if (cc < 1)
      break;
    buf[i++] = c;
    if (c == '\n' || c == '\r')
      break;
  }
  buf[i] = '\0';
  return buf;
}
```

大意就是从标准输入（屏幕读入）读取字符流，在遇到 \n \r 的时候完成一次读取。

我们的程序实现的时候也需要从标准输入读取字符流，因为是 -n1，也是在遇到 \n 的时候完成读取；我们在读完 buf 之后，还需要根据其中的空格来分割出一个个参数。

我认为这不如改造 gets，**添加额外的判断条件，使得在遇到空格的时候也完成一次读取**，读取多次直到读取结束。这样逻辑和 sh.c 的 main 就很相似了。

于是修改为：

```c
// ulib.c 的 gets 改造而来
char *ugets(char *buf, int max)
{
    int i, cc;
    char c;

    for (i = 0; i + 1 < max;)
    {
        cc = read(0, &c, 1);
        if (cc < 1)
            break;
        // 在遇到 \n \r 和 blank 时停止循环
        if (c == '\n' || c == '\r' || c == ' ')
            break;
        // 不把 \n \r 和 blank 读到字符串中
        buf[i++] = c;
    }
    buf[i] = '\0';
    return buf;
}
```

还需要注意的是，**原本的 gets 函数在遇到 \n 时候固然会停下，但是仍然会把 \n 写入字符串，然后在最后加上 \0。**而我们的是不需要这样的操作的，所以要注意把判断提前，避免写入 \n。

*在这一步卡了很久，一直没有发现问题，真是一个小难点！*

`sh.c` 的 getcmd 修改略。

## 3 返回来看 xargs

介绍：[Linux xargs 命令 | 菜鸟教程 (runoob.com)](https://www.runoob.com/linux/linux-comm-xargs.html)

> xargs 可以将管道或标准输入（stdin）数据转换成命令行参数，也能够从文件的输出中读取数据。
>
> 很多命令不支持|管道来传递参数。
>
> xargs 一般是和管道一起使用。
>
> xargs 用作替换工具，读取输入数据重新格式化后输出。

- 如何理解很多命令不支持|管道来传递参数？

eg1：wc 支持

```shell
$ echo hello world | wc
      1       2      12
```

eg2：echo 不支持

```shell
$ echo 1234 | echo 1234
1234
```

管道只负责传递两个程序之间的数据，把左边的程序的标准输出接到右边程序的标准输入上，他并没有完成这个数据，或者说字符串的格式化和参数的加入——也就是把它处理成右边程序的参数并加入到参数数组中。

echo，它本身并没有读取什么东西，更没有把读来的字符串处理成自己能接受的参数，所以收到管道传来的字符串的时候，他没有能力处理。

```c
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int main(int argc, char *argv[])
{
  int i;

  for (i = 1; i < argc; i++)
  {
    write(1, argv[i], strlen(argv[i]));
    if (i + 1 < argc)
    {
      write(1, " ", 1);
    }
    else
    {
      write(1, "\n", 1);
    }
  }
  exit(0);
}
```

而 wc，在没有其他参数的时候，它就自己从标准输入里读取数据，并进行处理，所以它可以支持管道传递参数。

- xargs 和 | 管道关系

xargs 没有负责传输，只是它只是把从标准输入来的数据处理，正如我们实现 xargs 时候一样；

| 管道才是真正负责传输数据的。

那么他们组合起来，才变成了“管道传递参数”，所以说 xargs 一般是和管道一起使用。

## 4 可能遇到的问题 - `grep: cannot open ./b`

出现于 grade 测试中，问题原因是未改造 gets 函数，讲解见上。
