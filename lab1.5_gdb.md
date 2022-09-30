# GDB 调试

## 0 参考集合

- GDB 调试学习（比较重要）
  - [GDB调试操作_颜 然的博客-CSDN博客_gdb 调试](https://blog.csdn.net/Yan__Ran/article/details/123692242)
  - [实战 GDB 调试 - 知乎 (zhihu.com)](https://zhuanlan.zhihu.com/p/530685908)
  - [GDB调试指南(入门，看这篇够了)_程序猿编码的博客-CSDN博客_gdb调试](https://blog.csdn.net/chen1415886044/article/details/105094688)
  - [代码调试—GDB使用技巧 - 知乎 (zhihu.com)](https://zhuanlan.zhihu.com/p/280101740)
  - [2.4 理解TTY窗口-CGDB中文手册 (cntofu.com)](https://www.cntofu.com/book/121/2.4.md)

- 关于 scanf（主要是解决在 GDB 调试过程中的输入数据的问题）
  - [关于调试时有scanf的情况_俗集的博客-CSDN博客](https://blog.csdn.net/weixin_50802839/article/details/115265818)
  - [救命啊！gdb调试程序，遇到程序要求输入，怎么才能输入数据！【linux吧】_百度贴吧 (baidu.com)](https://tieba.baidu.com/p/5887167677)
  - [当使用cgdb调试时，程序需要输入stdin的解决方法_OREH_HERO的博客-CSDN博客](https://blog.csdn.net/OREH_HERO/article/details/121213682)
- gdb 使用帮助——打开 gdb 之后 help

## 1 调试内容

很简单的一道题

素数求和

有N个数（0<N<1000），求N个数中所有素数之和。

**输入**

第一行给出整数M（0<M<10），代表有多少组测试数据；

每组测试数据第一行为N，表示该组测试数据的数量，接下来的N个数为要测试的数据，每个数都小于1000。

**输出**

每组测试数据结果占一行，输出测试数据的所有素数和。

**【示例1】**

```text
输入：
3
5
1 2 3 4 5
8
11 12 13 14 15 16 17 18
10
21 22 23 24 25 26 27 28 29 30
输出：
10
41
52
```

错误题解：

```c
#include <stdio.h>
#include <string.h>

int isS(int num)
{
    for (int i = 2; i < num / 2; i++)
    {
        if (num % i == 0)
        {
            // 整除
            return 0;
        }
    }

    return 1;
}

int main(void)
{
    int testcase = 0;
    scanf("%d", &testcase);

    int sum[10];                 // 每组数据的所有素数之和的数组
    memset(sum, 0, sizeof(sum)); // 清空

    int num = 0;
    int testdata = 0;

    for (int i = 0; i < testcase; i++)
    {
        scanf("%d", &num);
        for (int j = 0; j < num; j++)
        {
            scanf("%d", &testdata);

            if (j != num - 1)
            {
                scanf(" ");
            }

            if (isS(testdata))
            {
                sum[i] += testdata;
            }
        }
    }

    for (int i = 0; i < testcase; i++)
    {
        printf("%d\n", sum[i]);
    }

    return 0;
}
```

错因是没处理好 1 的情况，以及 4 的情况，需要补足 isS 函数。

正确题解：

```c
int isS(int num)
{
    if (num == 1)
    {
        return 0;
    }

    for (int i = 2; i <= num / 2; i++)
    {
        if (num % i == 0)
        {
            // 整除
            return 0;
        }
    }

    return 1;
}
```

用插入打印语句的方法很简单，但本篇需要用 GDB 来调试。