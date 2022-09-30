# MIT 6.S081 - Lab Utilities -- overview

记一下起始和结束。

实验一前提条件：

- xv6 book chapter 1
- 课程 lec01：[Lec01 Introduction and Examples (Robert) - MIT6.S081 (gitbook.io)](https://mit-public-courses-cn-translatio.gitbook.io/mit6-s081/lec01-introduction-and-examples)
- 部分 xv6 源码

主题：用 sys_call 写了几个 Unix 用户程序，熟悉一下 sys_call。

## 起始：配置

各种安装配置略。不细说。

## 结束：报告

### 一 回答问题

#### 1 阅读 sleep.c，回答下列问题

**a. 当用户在 xv6 的 shell 中，输入了命令 “sleep hello world\n"，请问在 sleep 程序里面，argc 的值是多少，argv 数组大小是多少。**

简单添加一行输出 argc。

```c
printf("argc = %d\n", argc);
```

test：

```shell
$ sleep hello world 
argc = 3
Sleep needs one argument!
```

argc 的值为 3。

简单添加一行输出。

```c
printf("size = %d * %d = %d\n", argc, sizeof(char *), argc * sizeof(char *));
```

test：

```shell
$ sleep hello world
size = 3 * 8 = 24
Sleep needs one argument!
```

argv 实质是指针数组，指针类型是字符指针。

c 语言的指针长度：指针反映的一个系统的最大寻址长度。32 位系统的指针位数是 4 字节，64 位系统的指针位数是 8 字节。

这里的 `sizeof(char *) = 8` ，所以大小是 24。



**b. 请描述上述第一道题 sleep 程序的 main 函数参数 argv 中的指针指向了哪些字符串，它们的含义是什么。**

添加循环输出 argv 的非 NULL 部分。`argv[argc] = NULL`

```c
for (int i = 0; i < argc; i++)
    printf("argv[%d]: %s\n", i, argv[i]);
printf("END: argv[%d]: %s\n", argc, argv[argc]);
```

test：

```shell
$ sleep hello world
argv[0]: sleep
argv[1]: hello
argv[2]: world
END: argv[3]: (null)
Sleep needs one argument!
```

argv 中的指针指向了哪些字符串如输出所示。第一个参数 sleep 就是程序的名字，不可以写错；后面的字符串是传入 sleep 程序的参数，但是 hello 和 world 是不符合 sleep 对参数数量的要求的。

*注意是数量而不是数字上出了问题，sleep 只需要除了 sleep 以外的一个参数，而 atoi 把不是数字的字符串也转成 int 了。linux 中并没有这个问题。*



**c. 哪些代码调用了系统调用为程序 sleep 提供了服务？**

- printf 间接调用了 write 的系统调用。
- exit、sleep 是直接使用了系统调用。

#### 2 了解管道模型，回答下列问题

**a. 简要说明你是怎么创建管道的，又是怎么使用管道传输数据的。**

创建管道：

```c
int p[2];
pipe(p);
```

向管道（写入端 p[1]）写数据：

```c
int i = 1;
write(p[1], &i, sizeof(int));
```

从管道（读出端 p[0]）读数据：

```c
int j;
read(p[0], &j, sizeof(int));
```



**b. fork 之后，我们怎么用管道在父子进程传输数据？**

设置情景：父进程向子进程传输长度为 sizeof(int) 的数据，1。

```c
#include "kernel/types.h"
#include "user/user.h"

int main()
{
    int p[2];
    pipe(p);

    if (fork() == 0)
    {
        close(p[1]);

        int j;
        if (read(p[0], &j, sizeof(int)) < 0)
        {
            printf("get wrong!\n");
            exit(1);
        }

        printf("j = %d\n", j);

        close(p[0]);
    }
    else
    {
        close(p[0]);

        int i = 1;
        write(p[1], &i, sizeof(int));

        close(p[1]);
        wait(0);
    }

    exit(0);
}
```



**c. 试解释，为什么要提前关闭管道中不使用的一端？（提示：结合管道的阻塞机制）**

```c
#include "kernel/types.h"
#include "user/user.h"

int main()
{
    int p[2];
    pipe(p);

    if (fork() == 0)
    {
        close(p[1]);

        int j;
        int read_r;
        while ((read_r = read(p[0], &j, sizeof(int))) > 0)
        {
            printf("get %d\n", j);
        }
        // 小于 0 就有问题，等于 0 就是正常的
        if (read_r < 0)
        {
            printf("get wrong!\n");
            exit(1);
        }

        close(p[0]);
    }
    else
    {
        close(p[0]);

        for (int i = 2; i <= 35; i++)
        {
            write(p[1], &i, sizeof(int));
        }
        close(p[1]);
        wait(0);
    }

    exit(0);
}
```

子进程并不会用到 `p[1]`，如果不最先关闭 `p[1]`（比如把这句话注释掉），那么程序就会在子进程输出完 2 - 35 之后停下且不结束。

当所有持有管道写入端的进程都把管道写入端关闭之后，read 把管道中剩下的字符串读出来之后就会返回 0；或者说，即使管道的缓冲区中没有字符了，只要有进程没有把管道写入端关闭，read 仍然会一直等待。（也就是需要所有持有管道写入端的进程都把管道写入端关闭才能结束堵塞并返回 0）

所以这里子进程不关闭 `p[1]` 就会让 read 堵塞，从而子进程无法 exit，父进程由于要等子进程结束才结束，所以也没能 exit。这样是不好的。

*顺带一提，即使是用完的端口，也要及时关闭。上面父进程在向管道写完数据之后不关闭的话，也会有一样的结果，原因也是一样的。*

### 二 实验详细设计

- [pingpong](http://www.sheniao.top/index.php/os/57.html)
- [primes](http://www.sheniao.top/index.php/os/61.html)
- [find (1)](http://www.sheniao.top/index.php/os/65.html)
- [find (2)](http://www.sheniao.top/index.php/os/68.html)
- [xargs](http://www.sheniao.top/index.php/os/69.html)

### 三 实验结果截图

![make_grade](https://typora-1304621073.cos.ap-guangzhou.myqcloud.com/typora/make_grade.png)

`time.txt` 里记录的时间是 360h（我花的时间真的很长）。

[Lab: Xv6 and Unix utilities (mit.edu)](https://pdos.csail.mit.edu/6.828/2020/labs/util.html)

按照 Submit the lab 把代码上传到了平台上。

![lab-util-handin](https://typora-1304621073.cos.ap-guangzhou.myqcloud.com/typora/lab-util-handin.jpg)

## END：sh.c 可以继续拓展

> The xv6 shell (`user/sh.c`) is just another user program and you can improve it.
>
> It is a minimal shell and lacks many features found in real shell.

这个 shell 确实还有很多可以改进的。有机会再来写吧！
