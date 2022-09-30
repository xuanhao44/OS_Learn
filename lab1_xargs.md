# MIT 6.S081 - Lab Utilities - xargs

## 0 大致了解 xargs

（linux 上运行）

```shell
$ echo 4321 8765 | xargs echo 1234 5678
1234 5678 4321 8765
```

xargs 后面才是需要执行的程序，即 echo，它后面已经接上了两个参数 1234 和 5678；而 xargs 的作用是把管道左边的程序的输出也作为右边的参数。比如在这里就等同于：

```shell
$ echo 1234 5678 4321 8765
1234 5678 4321 8765
```

> 实验中你不必考虑管道的实现与使用，你的程序可以直接从标准输入中读取管道传递给你的字符串。

这个很明显，因为管道的实现是由 sh 完成的。

这句话告诉我们，需要处理的东西只有如 `xargs echo 1234 5678`，然后 xargs 补充的参数是从标准输入 0 来的，也就是说我们要**从标准输入中读取字符串然后进行处理**。

## 1 关于参数处理

例子：

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
    argv[argc] = 0
    */

    for (int i = 0; i < argc; i++)
        printf("i = %d, %s\n", i, argv[i]);

    // 最后一个是 null，也可写作 0
    printf("END, but not in argv: i = %d, %s\n", argc, argv[argc]);

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

## 2 如何从标准输入中得到我们想要的参数

从标准输入中能够获取的，只是一串未经处理的字符串；而当输入缓冲区无数据时表示这个字符串读完了，这让人联想到 sh.c 中对于用户的屏幕输入的处理——于是我们去看 sh.c。

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

大意是从标准输入（屏幕读入）读取字符流，在遇到 \n \r 的时候完成一次读取。

我们的程序实现的时候也需要从标准输入读取字符流，当输入缓冲区无数据（输入的写端口被关闭）时表示这个字符串读完；我们在读完 buf 之后，还需要根据其中的空格来分割出一个个参数。

我认为这不如**改造 gets**，**添加额外的判断条件，使得在遇到空格的时候也完成一次读取**，读取多次直到读取结束。这样逻辑和 sh.c 的 main 就很相似了。

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

还需要注意的是，**原本的 gets 函数在遇到 \n 时候固然会停下，但是仍然会把 \n 写入字符串，然后在最后加上 \0。**而我们的是不需要这样的操作的，所以**要注意把判断提前，避免写入 \n**。

*在这一步卡了很久，一直没有发现问题，真是一个小难点！*

`sh.c` 的 getcmd 修改略。

## 3 框架测试

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

如果测试完有错误，可以去看实验框架在测试时生成的 log，名字为 `xv6.outxxxx`，可以打开看看输出了什么。下面展示是测试全部正确的 log。

```out
xv6 kernel is booting

hart 2 starting
hart 1 starting
init: starting sh
$ sh < xargstest.sh
$ echo DONE
$ $ $ $ $ hello
hello
hello
$ $ DONEqemu-system-riscv64: terminating on signal 15 from pid 4064 (make)
```

**PLUS**

这个测试的判错能力非常差，最好不要以为测试过了自己写的就是对的！！！

## 4 xargs 和管道的理解

> xargs 可以将管道或标准输入（stdin）数据转换成命令行参数，也能够从文件的输出中读取数据。
>
> 很多命令不支持|管道来传递参数。
>
> xargs 一般是和管道一起使用。
>
> xargs 用作替换工具，读取输入数据重新格式化后输出。

- 如何理解很多命令不支持 | 管道来传递参数？

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

echo，它本身并没有读取什么东西，更没有把读来的字符串处理成自己能接受的参数，所以收到管道传来的字符串的时候，他没有能力处理。（下面是 echo.c）

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

## 5 看看 xargs 部分的缺陷

xv6 是教学用的系统，其功能和完整性和现在的发行版 linux 之间有巨大的差别，而在这第一个实验的 xargs 的部分，我们就能窥见其一。我认为首先需要看看 linux 上已经做到什么程度，才能更好的指导我们去写 xv6 的 xargs；同时，我们也要明白 xv6 的局限性，以及我们的水平的局限性——（比如我们现在还没有实现过带 - 参数的程序）

### 5.1 linux - echo

首先考察 **linux** 中的 `echo`。

eg1：

```shell
$ echo 12 34
line 12 34
$ echo 12         34
line 12 34
```

可以发现 shell 处理了多余的空格。

eg2：

```shell
$ echo "line" "12    34" " 45"
line 12    34  45
```

可以发现加上双引号后 shell 就不会处理字符串里面的空格了。

eg3：

```shell
$ echo 1\n2
1n2
$ echo "1\n2"
1\n2
$ echo -e 1\n2
1n2
$ echo -e "1\n2"
1
2
```

可以发现在前两种情况下都没有处理好转义字符，只有加上参数 -e，并且位于字符串内才能处理。

-e 参数详情请 `man echo`。

### 5.2 linux - xargs

其次来考察 `xargs`。

eg4：

```shell
$ echo 1234 | xargs echo line
line 1234
```

不管是 echo 还是 xargs，目前都没有加参数，也就是默认的状态。

echo 的参数上面已经提到，而 xrags 的参数在下面的介绍中会提到两个：

- -d：delimited，分隔符参数
- -n：（详细解释见下）
  - *xargs 右侧命令，每次从输入获取，并加入参数列表，最后执行，的参数的个数*

### 5.2.1 -d

delimited，分隔符参数

> xargs reads items from the standard input, delimited by blanks (which can be protected with double or single quotes or a backslash) or newlines, and executes the command (default is /bin/echo) one or more times with any initial-arguments followed by items  read from standard input.  Blank lines on the standard input are ignored.

****

`man xargs`，开篇便说明 xargs 会把输入的字符串进行分割，并忽略掉空格、换行等等。

eg5：

```shell
$ echo -e "1       2" | xargs echo line
line 1 2
$ echo -e "1\n2" | xargs echo line
line 1 2
```

linux 里的 xargs 的能力确实非常强大。这个例子中，在默认的情况下，xargs 把""里的 blanks 也橄榄了。

eg6：

```shell
$ echo "12,34,56,78" | xargs echo line
line 12,34,56,78
$ echo "12,34,56,78" | xargs -d "," echo line
line 12 34 56 78

$ echo -n "12,34,56,78" | xargs -d "," echo line
line 12 34 56 78
```

第一个是默认情况。

第二个是以 "," 作为分隔符，那么字符串确实是这样被分隔开了；但是需要知道的是，echo 的末尾自带一个 \n，在默认情况下会被 xargs 去掉；但是加了 -d 参数后，所有的字符都被一视同仁，故而 \n 并没有被消除，也就会出现换行的情况了。

第三个是在第二个的基础上为左边的 echo 加上了 -n 参数，用了避免在末尾加上 \n。

### 5.2.2 -n

> -n max-args, --max-args=max-args
>            Use  at  most  max-args  arguments per command line.  Fewer than max-args arguments will be used if the size (see the -s option) is exceeded, unless the -x option is given, in which case xargs will exit.

先来看一张图示。

<img src="https://typora-1304621073.cos.ap-guangzhou.myqcloud.com/typora/xargs%E5%9B%BE%E8%A7%A3.jpg" alt="xargs图解" style="zoom: 33%;" />

用实际的例子来继续讲解。

eg7：

```shell
$ find . b
.
./a
./a/b
./.editorconfig
./gradelib.py
./.gitignore
./__pycache__
./__pycache__/gradelib.cpython-38.pyc
./grade-lab-util
./LICENSE
./xv6.out
./conf
./conf/lab.mk
./.git
./.git/index
./.git/HEAD
...(more)
$ find . b | xargs echo line
line . ./a ./a/b ./.editorconfig ./gradelib.py ./.gitignore ./__pycache__ ./__pycache__/gradelib.cpython-38.pyc ./grade-lab-util ./LICENSE ./xv6.out ./conf ./conf/lab.mk ./.git ./.git/index ./.git/HEAD ./.git/branches ./.git/packed-refs ./.git/config ./.git/description ./.git/FETCH_HEAD ./.git/hooks ./.git/hooks/fsmonitor-watchman.sample ./.git/hooks/update.sample ./.git/hooks/pre-applypatch.sample ./.git/hooks/pre-push.sample ./.git/hooks/pre-receive.sample
...(more)
```

可以看到 xargs 在默认的情况下，把前面 find 的输出经过分隔处理后，将分隔的每一个参数都作为后面 echo 的参数了。

line 只被输出了一次，用 xv6 的知识可以推断，echo 命令只被执行了一次，所有的参数都被加入到这次执行的参数列表中了。

eg8：

```shell
$ find . b | xargs -n 1 echo line
line .
line ./a
line ./a/b
line ./.editorconfig
line ./gradelib.py
line ./.gitignore
line ./__pycache__
line ./__pycache__/gradelib.cpython-38.pyc
line ./grade-lab-util
line ./LICENSE
...(more)
$ find . b | xargs -n 2 echo line
line . ./a
line ./a/b ./.editorconfig
line ./gradelib.py ./.gitignore
line ./__pycache__ ./__pycache__/gradelib.cpython-38.pyc
line ./grade-lab-util ./LICENSE
line ./xv6.out ./conf
line ./conf/lab.mk ./.git
line ./.git/index ./.git/HEAD
line ./.git/branches ./.git/packed-refs
```

被每次打印的 line 说明 echo 执行多次；可以看到，`-n 1` 使得每次传给 echo 去执行的附加参数只有一个，`-n 2` 就是两个。

*有的中文文档说 -n 是格式化输出，1 就是一行一个，2 就是一行两个，实际上这种说法完全不得要领也没说明实质。*

## 5.3 现在是降维打击时间

以我们上面的知识去看 MIT 的指导书和我们学校的指导书。

MIT eg9（**in UNIX**）：

```shell
$ echo "1\n2" | xargs -n 1 echo lin
line 1
line 2
```

MIT 指导书上是这么写的。这是在 UNIX 程序中的正确输出。但不是在 linux 中的。

在 linux 中，我们补上左边 echo 的参数 -e，实际应该是：

eg9（正确例）：

```shell
$ echo -e "1\n2" | xargs -n 1 echo line
line 1
line 2
```

HITSZ eg10（in xv6，-n1 参数，but 错误例）：

```shell
$ xargs echo good   # 指定要执行的命令：echo，同时输入参数'good'
bye                 # 换行后继续输入echo的参数'bye'
good bye            # 执行"echo good bye"，输出"good bye"
hello too           # 换行后输入参数'hello too'
good hello too      # 执行"echo good hello too"，输出"good hello too"
# 通过ctrl+D结束输入
```

这里面 good bye 中间由于有空格，会被 sh 处理并分成两个参数。如果想要获得类似的效果，你需要在中间加一个连接符，比如 -。

HITSZ eg11（in linux，-n2 参数，but 错误例）：

```shell
$ xargs -n2 echo good   # 设置选项-n为2，表示接收两个参数（两行输入）；指定要执行的命令：echo，并输入参数'good'
bye                     # 换行后输入参数'bye'
hello too               # 换行后继续输入参数'hello too'，至此接收两个参数
good bye hello too      # 执行"echo good bye hello too"，输出"good bye hello too"
# 通过ctrl+D结束输入
$
```

和上面一样的错法和修改方案。

*直接使用 xargs 的情况不算多。*

值得注意的一点是：在 linux 中，当键盘输入的参数满足 -n（如 -n1）的时候，就会输出一句，比如 eg10 中途输出的 good bye。这一点没有在要求中实现，事实上，我们在写的时候可以去考虑这个问题，也可以不考虑——也就是直接等输入完全结束后输出。

## 5.4 代码实现

```c
#include "kernel/types.h"
#include "user/user.h"
#include "kernel/param.h"

// ulib.c 的 get 改造而来
char *ugets(char *buf, int max);

// sh.c 的 getcmd 改造而来
int ugetcmd(char *buf, int nbuf);

// 向字符指针数组后面添加一个指针
void append1(int *argc, char *argv[], char *token);

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

    // buf 是从标准输入读数据的缓冲区
    char buf[100];
    // token 是实际存这些输入的空间
    char token[MAXARG][100];

    int len = 0; // token 长度
    while (ugetcmd(buf, sizeof(buf)) >= 0)
        strcpy(token[len++], buf); // buf 一直在变，不能添加到字符串指针数组里

    for (int i = 0; i < len; i++)
    {
        if (fork() == 0)
        {
            // -n 1
            append1(&argc_alter, argv_alter, token[i]);
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
    }

    exit(0);
}

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

// 向字符指针数组后面添加一个指针
void append1(int *argc, char *argv[], char *token)
{
    argv[*argc] = token;
    (*argc)++;
    argv[*argc] = 0;
}
```

## 6 犯过的错

1. gets 改造的时候没有处理到“不能把 \n、空格读入字符串”的要求。
2. 没有使用二维数组保存处理好的字符串，还犯了一个初学者的错误。下面细说。



当时的部分代码（当时思路是把所有参数加到参数列表中，还没有实现 -n1）

```c
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
```

这段代码中，循环内部定义了一个字符数组 temp，并用这个 temp 拷贝了 buf，并把 temp 加入到字符串指针数组中，希望通过这样的方式加入字符串。

但是这样是不对的。当时的代码出现了一个比较奇怪的情况就是：

```shell
$ echo 1 2 3 4 | xargs echo line
line 4 4 4 4
```

最后一个字符串 4，像是覆盖掉了之前的字符串？



这段代码的问题有两个：

1. 块中的局部变量是不可以在外面访问的，在外面访问就是非法的。也就是说上面代码存到指针数组中的指针，全部都是**悬空指针**。
2. 循环内部定义的变量的地址的问题：虽然每次循环的时候，这个变量都被释放了，这个地址没有对应变量，但是在下一次循环的时候，这个变量的地址又被编译器选在了和上一次在栈上相同的位置——也就是有着相同的地址。也就是说上面代码存到指针数组中的指针指向的地址，都是同一块。



这也是个经典问题：[For-loop Local Variables in C - Stack Overflow](https://stackoverflow.com/questions/5136393/for-loop-local-variables-in-c)



你也可以自己写一份体会一下问题所在：

```c
#include <stdio.h>

int main()
{
    int x = 10;
    int *p;
    while (x >= 0)
    {
        x--;
        int y;
        y = x;
        printf("y = %d,&y = %d\n", y, &y);
        p = &y;
    }

    printf("\n");
    printf("*p = %d\n", *p);
    return 0;
}
```

输出为：

```c
y = 9,&y = 6422028
y = 8,&y = 6422028
y = 7,&y = 6422028
y = 6,&y = 6422028
y = 5,&y = 6422028
y = 4,&y = 6422028
y = 3,&y = 6422028
y = 2,&y = 6422028
y = 1,&y = 6422028
y = 0,&y = 6422028
y = -1,&y = 6422028

*p = -1
```
