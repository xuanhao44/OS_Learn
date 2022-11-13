# MIT 6.S081 - Lab Utilities - find (2) - 写 find.c

这次就把 find 写完！

## 0 回顾 & 补充

上一篇的链接：[MIT 6.S081 - Lab Utilities - find (1) - 先看看 ls.c - 珅鸟玄's Blog (sheniao.top)](http://www.sheniao.top/index.php/os/65.html)

上一篇讲了：

1. 文件系统
2. `ls.c`

还有几个问题需要注意，而这个问题也是本次的一个重点。

### 0.1 T_DEVICE

xv6 中输入 `ls`：

```shell
$ ls
.              1 1 1024
..             1 1 1024
README         2 2 2059
xargstest.sh   2 3 93
cat            2 4 23976
echo           2 5 22912
forktest       2 6 13176
grep           2 7 27328
init           2 8 23904
kill           2 9 22776
ln             2 10 22728
ls             2 11 26216
mkdir          2 12 22880
rm             2 13 22864
sh             2 14 41752
stressfs       2 15 23880
usertests      2 16 147512
grind          2 17 37992
wc             2 18 25112
zombie         2 19 22272
sleep          2 20 22832
copy           2 21 22496
open           2 22 22360
fork           2 23 22520
exec           2 24 22488
forkexec       2 25 23104
redirect       2 26 23096
pipe           2 27 22920
pingpong       2 28 23232
primes         2 29 24376
pipe_read      2 30 23288
fstat          2 31 22928
find           2 32 26736
console        3 33 0
```

注意到最后面有：名为 `console` 的文件，文件的类型为 `T_DEVICE`，即 3。

而在之前 `ls.c` 的研究中发现，在 switch 语句中没有 `T_DEVICE` 的 case。那为什么输出了 `console` 呢？

原因是：如果 ls 的 path 是目录，那么 ls 会输出 目录下的所有文件，这个输出是不分文件类型的，所以 `console` 也会被输出出来。

既然如此，那可以尝试 `ls console`，看看效果。结果是：不显示任何东西，xv6 的 ls 处理不了，这就符合预期了。

### 0.2 `.` 和 `..`

- `.` 指当前目录
- `..` 指上一级目录

这是之前就知道的，为何要提起呢？因为 xv6 的 ls 输出了这两个"文件"！

看上面的 shell 的输出，可以发现头两个就是这两个目录，它们也被 ls 输出出来。

它们也是能被 `stat(path, &st)` 处理的，意味着 `./.` 和 `./..` 这样的路径也是能被识别的。

在 Linux 中，只使用 ls 并不会显示 `.` 和 `..`。

- -a：显示所有文件及目录 (ls 内定将文件名或目录名称开头为 "." 的视为隐藏档，不会列出)
- -A：同 -a ，但不列出 "." (目前目录) 及 ".." (父目录)

### 0.3 更多

问题：为啥 ls 不能执行？

```c
$ echo > b
$ mkdir a
$ echo > a/b
$ cd a
$ ls
exec ls failed
```

原因：因为 ls 是用户程序，执行的时候不在它的目录里肯定跑不了！

在上一级目录里去执行它就好了：

```shell
$ ../ls
.              1 35 48
..             1 1 1024
b              2 36 2
```

*笨笨刘哈哈。*

## 1 先搓个框架

```c
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"
#include "kernel/fcntl.h"

void find(char *path, char *name);

int main(int argc, char *argv[])
{
    if (argc < 3)
    {
        // 缺参数
        fprintf(2, "usage: find path name\n");
        exit(1);
    }

    char *path = argv[1];
    char *name = argv[2];

    find(path, name);

    exit(0);
}
```

## 2 完整实现

函数 `fmtname` 来源于 `ls.c`，但是进行了修改：不在文件名的后面添加空格了，很 pure 的文件名。

*照抄 ls.c 里的是不行的！因为 static char 指向的是同一个东西 buf，用于比较是不可以的！*

<img src="https://typora-1304621073.cos.ap-guangzhou.myqcloud.com/typora/find%E9%80%92%E5%BD%92%E9%80%BB%E8%BE%91.jpg" alt="find递归逻辑" style="zoom:33%;" />

**注意到要去掉 `.` 和 `..`，避免循环递归！**

*FILE 文件名的匹配也可以在 DIR 的第三步进行。*

```c
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"

char *fmtname(char *path);

void find(char *path, char *name);

int main(int argc, char *argv[])
{
    if (argc < 3)
    {
        // 缺参数
        fprintf(2, "usage: find path name\n");
        exit(1);
    }

    char *path = argv[1];
    char *name = argv[2];

    find(path, name);

    exit(0);
}

char *fmtname(char *path)
{
    char *p;

    // Find first character after last slash.
    for (p = path + strlen(path); p >= path && *p != '/'; p--)
        ;
    p++;

    // Return blank-padded name.
    return p;
}

void find(char *path, char *name)
{
    char buf[512], *p; // 完整目录存储的字符数组（大概），p 是用来操作这个数组的指针
    int fd;
    struct dirent de; // 目录项
    struct stat st;   // 存储文件信息的结构体

    // 尝试按照路径打开文件
    if ((fd = open(path, 0)) < 0)
    {
        // 打不开就算了
        fprintf(2, "find: cannot open %s\n", path);
        return;
    }

    // 尝试查看文件信息
    if (fstat(fd, &st) < 0)
    {
        // 查不到就算了
        fprintf(2, "find: cannot stat %s\n", path);
        close(fd);
        return;
    }

    switch (st.type)
    {
    case T_FILE:
        // 文件
        if (strcmp(fmtname(path), fmtname(name)) == 0)
        {
            printf("%s\n", path);
        }
        break;

    case T_DIR:
        // 目录
        if (strlen(path) + 1 + DIRSIZ + 1 > sizeof buf)
        {
            // path 目录的基础上查看目录里的文件还要加上 '/' + 一个目录项 + '/' （大概）
            // 加上这就超出 512 了就说明 path 太长了
            printf("find: path too long\n");
            break;
        }

        // 先复制一份 path 到 buf
        strcpy(buf, path);
        // 操作指针 p 也指向字符串的末尾
        p = buf + strlen(buf);
        // 补上一个 / 然后右移一位
        *p++ = '/';

        // 从 fd 里读目录项
        while (read(fd, &de, sizeof(de)) == sizeof(de))
        {
            // 没内容就跳过
            if (de.inum == 0)
                continue;

            // 拒绝循环递归
            int judge = strcmp(de.name, ".") && strcmp(de.name, "..");
            if (judge == 0)
                continue;

            // 有内容就把目录项名字的字符串复制到 buf 中（通过指针 p）
            // 可以发现，复制好像把空格也复制进去了
            memmove(p, de.name, DIRSIZ);
            p[DIRSIZ] = 0;

            // 剩下的不管是文件还是目录要 find
            find(buf, name);
        }
        break;
    }
    close(fd);
}
```

