# MIT 6.S081 - Lab Utilities - primes

## 1 简单的管道传输练手

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
        for (int i = 2; i <= 35; i++)
        {
            read(p[0], &j, sizeof(int));
            printf("prime %d\n", j);
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

需要注意几个小问题。

### 1.1 先创建管道，后 fork()

错误示范：此 pipe p 非彼 pipe p

```c
#include "kernel/types.h"
#include "user/user.h"

int main()
{
    int pid = fork();

    int p[2];
    pipe(p);

    if (pid == 0)
    {
        close(p[1]);
        ...
        close(p[0]);
    }
    else
    {
        close(p[0]);
        ...
        close(p[1]);
    }

    exit(0);
}
```

在 fork 之后创建的管道，实质上是两个进程分别创建了名字相同但是实质并不是一个的管道，因此并没有达成一个管道被两个进程所持有的情况，是失败的。

### 1.2 read 和 write 的参数和返回值

错误示范 1：

```c
write(p[1], 1, sizeof(int));
```

错误示范 2：

```c
int j;
read(p[0], j, sizeof(int));
```

read 和 write 的定义是：

```c
int read(int, void*, int);
int write(int, const void*, int);
```

注意到 read 和 write 的数据来源，第二个参数填写的是存放数据的地址，即指针。

所以改为：

```c
int i = 1;
write(p[1], &i, sizeof(int));
```

和

```c
int j;
read(p[0], &j, sizeof(int));
```

这里 read 会返回 4，即 sizeof(int) 4 字节。

## 2 利用管道传输时 read 的特性

如果某个管道的写端口都被关闭了，那么 read 不会一直等下去，而是返回 0。

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
        while (read(p[0], &j, sizeof(int)) != 0)
        {
            printf("prime %d\n", j);
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

子进程在一开始也关闭了写管道，而父进程在写完之后就关闭了写管道，故而在父进程在写完并关闭写管道后，read 一定是能返回 0，也就是说明读结束了。

### 2.1 实验 1 - 父进程不关闭 p[1]，或者先 wait 再关闭 p[1]

简单来说就是把父进程的代码修改一部分：

第一种：

```c
// close(p[1]);
wait(0);
```

第二种：

```c
wait(0);
close(p[1]);
```

会有什么效果？（提示：这两种都没有关闭或者及时关闭管道的写端口。）

第一种：程序在输出完所有的东西后就卡住了。

父进程在写完之后就开始等子进程结束；然后子进程这边由于父进程没有关闭管道的写端口，所以即使管道内已经没有数据了，但 read 只能等待而不是返回 0，那么子进程就在这里被卡住了（注意不是一直在循环，因为 while 循环也被卡住了），故而父进程也结束不了。

第二种：完全一样的效果和原因。父进程没可能执行到 close(p[1])。

### 2.2 实验 2 - 父进程不 wait 了，直接关闭 p[1] 走人

```c
close(p[1]);
// wait(0);
```

要确保子进程先退出，父进程再退出。要不然容易出现如下错误，即 $ 符号先打印出来，但是子进程还在运行状态中。

```shell
$ pipe_read
prime$  2
prime 3
prime 4
prime 5
prime 6
prime 7
prime 8
prime 9
...
```

推测 $ 出现是因为父进程结束了，可以看到父进程结束的相当早。

然后这还会导致检查用的 py 判断判错，注意。

## 2.3 学习 cat，添加错误处理

cat 在这一块写的真好。

主要是要把 read 处理好了。

```c
// pipe_read.c

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
            printf("prime %d\n", j);
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

## 3 埃氏筛和实现

介绍略。

伪码：

```c
p = get a number from left neighbor
print p
loop:
    n = get a number from left neighbor
    if (p does not divide n)
        send n to right neighbor
```

筛选思路：使用系统调用 pipe 创建管道，使用系统调用 fork 创建子进程。主进程将所有数据（例 2~11）输入到管道的左侧，第一个子进程从管道读出并筛选出 2，排除掉 2 的倍数，其他数字再写入下一管道；第二个子进程读出并筛选出 3，排除掉 3 的倍数，其他数字写入到下一管道；第三个子进程读出筛选出 5，以此类推……如下图所示：

![埃氏筛-管道](https://typora-1304621073.cos.ap-guangzhou.myqcloud.com/typora/%E5%9F%83%E6%B0%8F%E7%AD%9B-%E7%AE%A1%E9%81%93.png)

上图中，首先将 2 的整数倍的数去掉，再将 3 的整数倍的数去掉，依次类推。

很明显会有一个递归。

<img src="https://typora-1304621073.cos.ap-guangzhou.myqcloud.com/typora/primes%E5%9B%BE%E7%A4%BA%E9%80%92%E5%BD%92%E9%80%BB%E8%BE%91.png" alt="primes图示递归逻辑" style="zoom:50%;" />

最终代码如下：

```c
// primes.c

#include "kernel/types.h"
#include "user/user.h"

void child(int *p)
{
    // 关闭上一个进程的管道的写端口
    close(p[1]);

    int i;
    int read_r = read(p[0], &i, sizeof(int));

    if (read_r == 0)
    {
        // 没有数被传到这个进程来 该结束了
        close(p[0]);
        exit(0);
    }
    else if (read_r > 0)
    {
        // 每次，或者说每个子进程只打印当前那个最小的数，也即那个素数
        printf("prime %d\n", i);
    }
    else
    {
        // -1
        printf("get wrong!\n");
        exit(1);
    }

    // 先创建管道，关闭读端口
    int temp_p[2];
    pipe(temp_p);

    if (fork() == 0)
    {
        // 子进程 递归
        child(temp_p);
    }
    else
    {
        // 当前进程

        // 首先关闭与下一个进程交流的管道的读端口
        close(temp_p[0]);

        int j;
        while ((read_r = read(p[0], &j, sizeof(int))) > 0)
        {
            // 如果不会被整除，就把它塞到新管道中。让下一个进程处理
            if (j % i != 0)
            {
                write(temp_p[1], &j, sizeof(int));
            }
        }
        // 小于 0 就有问题，等于 0 就是正常的（上面已经输出完最后一个数了）
        if (read_r < 0)
        {
            // -1
            printf("get wrong!\n");
            exit(1);
        }

        // 最后关闭 p[0] 读端口，也关闭 temp_p[1] 写端口
        close(p[0]);
        close(temp_p[1]);

        // 等待，等到了子进程返回，自己也结束了
        wait(0);
        exit(0);
    }
}

int main()
{

    int p[2];
    pipe(p);

    if (fork() == 0)
    {
        child(p);
    }
    else
    {
        close(p[0]);

        for (int i = 2; i <= 35; i++)
        {
            write(p[1], &i, sizeof(int));
        }
        // 写完就要关掉!
        close(p[1]);

        wait(0);
    }

    exit(0);
}
```

