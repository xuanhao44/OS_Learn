# MIT 6.S081 - Lab Utilities - pingpong

老实说，pingpong 的问题的描述很烂，你不需要用 pipe 就可以完成这个部分（通过 py 测试）。

##  0 pingpong 无 pipe

```c
// pingpong.c

#include "kernel/types.h"
#include "user/user.h"

int main()
{
    int pid = fork();

    int status;

    if (pid == 0)
    {
        /* 子进程 */
        printf("%d: received ping\n", getpid());
    }
    else
    {
        /* 父进程 */

        wait(&status);

        printf("%d: received pong\n", getpid());
    }

    exit(0);
}
```

当然，题目是希望我们用 pipe 的，那就用吧。

## 1 pingpong 使用两个 pipe

```c
// pingpong.c

#include "kernel/types.h"
#include "user/user.h"

int main()
{
    // 父进程向子进程传 ping，子进程向父进程传 pong

    int p1[2], p2[2];
    pipe(p1);
    pipe(p2);
    // p1: child  读出 <- p1[0]-p1[1] <- parent 写入
    // p2: parent 读出 <- p2[0]-p2[1] <- child  写入

    int pid = fork();
    int status;

    char buf[64];

    if (pid == 0)
    {
        /* 子进程 */

        close(p2[0]);
        close(p1[1]);
        // 只要不要让两个 read 都在前面就行，避免死锁
        write(p2[1], "pong", 4);
        read(p1[0], buf, 4);
        close(p1[0]);
        close(p2[1]);

        printf("%d: received %s\n", getpid(), buf);
    }
    else
    {
        /* 父进程 */

        close(p2[1]);
        close(p1[0]);
        write(p1[1], "ping", 4);
        wait(&status);
        read(p2[0], buf, 4);
        close(p1[1]);
        close(p2[0]);

        printf("%d: received %s\n", getpid(), buf);
    }

    exit(0);
}
```

## 1.x pingpong 大家都在等数据

如果父进程和子进程都先 read 会怎么样？答案是会出现经典死锁。

```c
// pingpong.c

#include "kernel/types.h"
#include "user/user.h"

int main()
{
    // 父进程向子进程传 ping，子进程向父进程传 pong

    int p1[2], p2[2];
    pipe(p1);
    pipe(p2);
    // p1: child  读出 <- p1[0]-p1[1] <- parent 写入
    // p2: parent 读出 <- p2[0]-p2[1] <- child  写入

    int pid = fork();

    char buf[64];

    if (pid == 0)
    {
        /* 子进程 */

        close(p2[0]);
        close(p1[1]);

        read(p1[0], buf, 4);
        write(p2[1], "pong", 4);

        close(p1[0]);
        close(p2[1]);

        printf("%d: received %s\n", getpid(), buf);
    }
    else
    {
        /* 父进程 */

        close(p2[1]);
        close(p1[0]);

        read(p2[0], buf, 4);
        write(p1[1], "ping", 4);

        close(p1[1]);
        close(p2[0]);

        printf("%d: received %s\n", getpid(), buf);
    }

    exit(0);
}
```

程序现状：

<img src="https://typora-1304621073.cos.ap-guangzhou.myqcloud.com/typora/pingpong-read%E6%AD%BB%E9%94%81.png" alt="pingpong-read死锁" style="zoom: 50%;" />

父进程和子进程的 read 都在等待管道里会有对方写入的数据，所以都陷入了等待。

## 2 pingpong 尝试使用一个 pipe

```c
// pingpong.c

#include "kernel/types.h"
#include "user/user.h"

int main()
{
    // 父进程向子进程传 ping，子进程向父进程传 pong
    // 我希望先显示子进程 ping，后显示父进程 pong
    // 若不使用 wait，就需要利用管道的读写阻塞

    int p[2];
    pipe(p);

    int pid = fork();

    char buf[64];

    int status;

    if (pid == 0)
    {
        /* 子进程 */

        read(p[0], buf, 4);
        close(p[0]);

        printf("%d: received %s\n", getpid(), buf);

        write(p[1], "pong", 4);
        close(p[1]);
    }
    else
    {
        /* 父进程 */

        write(p[1], "ping", 4);
        close(p[1]);

        wait(&status);

        read(p[0], buf, 4);
        close(p[0]);

        printf("%d: received %s\n", getpid(), buf);
    }

    exit(0);
}
```

不加 wait 就可能会出错。

正常输出一般可能为：

```shell
4: received ping
3: received pong
```

而某一种情况输出为：

```shell
3: received ping
```

如何解释这种不正常的输出？

这是因为在这里父进程的 read 会先于子进程的 read 读出管道内的数据，这样子进程的 read 就阻塞了，成了孤儿进程，而父进程则继续执行下去，输出了本该交给子进程的数据。所以输出了怪异的 `3: received ping`。

可以认为，父进程的 read 会和子进程的 read 争抢 write 到管道内的 ping，若是父进程抢到就出错，子进程抢到就正常运行。

程序现状：

<img src="https://typora-1304621073.cos.ap-guangzhou.myqcloud.com/typora/%E4%BA%89%E6%8A%A2ping.png" alt="争抢ping" style="zoom:50%;" />

而在给父进程加上 wait 之后，父进程的 read 就只能等待子进程结束了，这样就保证不会出错。

同时需要说明，确实是不推荐用一个管道来解决问题的，还是使用两个管道吧。
