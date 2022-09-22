# MIT 6.S081 - Lab Utilities - find (1) - 先看看 ls.c

在写 find 之前，先来研究 `ls.c`。**本篇的目的就是把 `ls.c` 弄懂。**（真的很难啊）

## 1 文件系统

在看 `ls.c` 之前，先要学一下文件系统知识。

知识来源于[第零章 操作系统接口 | xv6 中文文档 (gitbooks.io)](https://th0ar.gitbooks.io/xv6-chinese/content/content/chapter0.html)，老实说写的很简略，我看完不是很满意。

### 1.1 文件和目录

- 目录也是文件：xv6 把目录实现为一种特殊的文件。
- `/` 是根目录，不从 `/` 开始的目录表示的是相对调用进程当前目录的目录。

### 1.2 chdir, mkdir, mknod

调用进程的当前目录可以通过 `chdir` 这个系统调用进行改变。

```c
chdir("/a");
chdir("b");
```

将当前目录切换到 `/a/b`。

mkdir 和 mknod 略。

### 1.3 stat, fstat 与文件名, 文件

`fstat` 可以获取一个文件描述符指向的文件的信息。它填充一个名为 `stat` 的结构体。

```c
#define T_DIR  1
#define T_FILE 2
#define T_DEV  3
// Directory
// File
// Device
     struct stat {
       short type;  // Type of file
       int dev;     // File system’s disk device
       uint ino;    // Inode number
       short nlink; // Number of links to file
       uint size;   // Size of file in bytes
};
```

fstat 本身似乎有 `fstat(fd)` 和 `fstat(fd, &stat)` 的用法。

文件名和这个文件本身是有很大的区别。

- 同一个文件（称为 `inode`）可能有多个名字，称为**连接** (`links`)。

- 每一个 inode 都由一个唯一的 `inode 号` 直接确定。

- 系统调用 `link` 创建另一个文件系统的名称，它指向同一个 `inode`。
  - 故而不同的文件名可能指向同一个文件，查看 `nlink` 数也不会是 1。
  
- 系统调用 `unlink` 从文件系统移除一个文件名。一个文件的 inode 和磁盘空间只有当它的链接数变为 0 的时候才会被清空，也就是没有一个文件再指向它。

  - 创建一个临时 inode 的最佳方式，这个 inode 会在进程关闭 `fd` 或者退出的时候被清空。

    ```c
    fd = open("/tmp/xyz", O_CREATE|O_RDWR);
    unlink("/tmp/xyz");
    ```

## 2 结合 ls.c 的理论初实践

ls.c 是很好的参考对象，接下来开始一步步学习。

```c
// fstat.c

#include "kernel/types.h"
#include "user/user.h"
#include "kernel/fcntl.h"
#include "kernel/stat.h"
#include "kernel/fs.h"

int main()
{
    printf("---------------------------------------------------\n");

    /* 1. 查看文件的类型 */
    printf("1. file's type\n");
    // 创建一个名为 output.txt 的文件
    int fd_1 = open("output.txt", O_CREATE | O_WRONLY);

    struct stat st_1;
    fstat(fd_1, &st_1); // 获取一个文件描述符指向的文件的信息

    printf("output.txt 's type is %d\n", st_1.type);

    // . 指的是当前文件目录, 打开的方式是仅读
    int fd_2 = open(".", O_RDONLY);

    struct stat st_2;
    fstat(fd_2, &st_2);

    printf("\".\" 's type is %d\n", st_2.type);

    int fd_3 = 1;

    struct stat st_3;
    fstat(fd_3, &st_3);

    printf("stdout 's type is %d\n", st_3.type);

    printf("---------------------------------------------------\n");

    /* 2. 连接的实验 */
    printf("2. link output.txt -> output_alter.txt\n");

    link("output.txt", "output_alter.txt");

    int fd_alter = open("output_alter.txt", O_RDONLY);

    struct stat st_alter;
    fstat(fd_alter, &st_alter);

    // 查看 inode
    // 似乎没有对 uint 规定 %u 的格式符，还是用 %d
    printf("output.txt 's inode number is %d\n", st_1.ino);
    printf("output_alter.txt 's inode number is %d\n", st_alter.ino);
    // 查看 nlink
    printf("output_alter.txt 's nlink number is %d\n", st_alter.nlink);
    // 需要更新一下信息，不然 nlink 不会变
    fstat(fd_1, &st_1);
    printf("output.txt 's nlink number is %d\n", st_1.nlink);

    printf("---------------------------------------------------\n");

    /* 3. 解除连接的实验 */
    printf("3. unlink\n");

    unlink("output_alter.txt");
    fstat(fd_1, &st_1);
    printf("output.txt 's nlink number is %d\n", st_1.nlink);

    unlink("output.txt");
    fstat(fd_1, &st_1);
    printf("output.txt 's nlink number is %d\n", st_1.nlink);

    /* END */
    // 按说这个阶段, "output.txt" 对应的 inode 就被清空了
    close(fd_1);
    close(fd_2);
    close(fd_alter);

    exit(0);
}
```

输出：

```shell
---------------------------------------------------
1. file's type
output.txt 's type is 2
"." 's type is 1
stdout 's type is 3
---------------------------------------------------
2. link output.txt -> output_alter.txt
output.txt 's inode number is 33
output_alter.txt 's inode number is 33
output_alter.txt 's nlink number is 2
output.txt 's nlink number is 2
---------------------------------------------------
3. unlink
output.txt 's nlink number is 1
output.txt 's nlink number is 0
```

### 2.1 open 参数

查看 `fcntl.h`

```c
#define O_RDONLY  0x000
#define O_WRONLY  0x001
#define O_RDWR    0x002
#define O_CREATE  0x200
#define O_TRUNC   0x400
```

理解：

|   flag   |        说明         |
| :------: | :-----------------: |
| O_RDONLY |        只读         |
| O_WRONLY |        只写         |
|  O_RDWR  |       读和写        |
| O_CREATE |    不存在时新建     |
| O_TRUNC  | 把文件截断到 0 长度 |

这些都是用 bit 描述的，可以做或运算：

```c
int fd_1 = open("output.txt", O_CREATE | O_WRONLY);
```

不引用 `fcntl.h` 也可以，直接使用数，ls.c 就是这样做的：

```c
if ((fd = open(path, 0)) < 0)
  {
    fprintf(2, "ls: cannot open %s\n", path);
    return;
  }
```

### 2.2 `.` 代表当前目录

从 ls.c 里看来的，感觉很神奇。

- 当前目录使用小数点“.”来表示；
- “..”代表上级目录；
- “./”表示下级目录。

### 2.3 文件类型

```c
#define T_DIR  1
#define T_FILE 2
#define T_DEV  3
```

可以看到目录的 type 是 1，文件的 type 是 2。

设备的 type 是 3。最常见的设备大概就是 stdin、stdout 和 stderr 吧，可以直接 `fstat(1, &st);` 查看。

## 3 ls.c 中遇到的没学过的知识

网上对这部分知识的介绍要不是没有，要不是讲 linux 而不是 xv6，要不是讲文件系统过于深入...总之没有适合初学者的介绍。这下只能靠自己的测试和前辈的指点了。

### 3.1 dirent - 目录项

在 `fs.h` 中的定义：

```c
// Directory is a file containing a sequence of dirent structures.
#define DIRSIZ 14

struct dirent {
  ushort inum;
  char name[DIRSIZ];
};
```

测试代码：

```c
#include "kernel/types.h"
#include "user/user.h"
#include "kernel/fcntl.h"
#include "kernel/stat.h"
#include "kernel/fs.h"

int main()
{
    // 打开根目录
    int fd = open("/", O_RDONLY);

    struct dirent de;
    struct stat st;

    // 获取文件信息
    fstat(fd, &st);

    while (read(fd, &de, sizeof(de)) == sizeof(de))
    {
        printf("inum = %d, name = %s\n", de.inum, de.name);
    }

    close(fd);
    exit(0);
}
```

输出：

```shell
inum = 1, name = .
inum = 1, name = ..
inum = 2, name = README
inum = 3, name = xargstest.sh
inum = 4, name = cat
inum = 5, name = echo
...(省略)
inum = 32, name = console
inum = 0, name = 
...(省略)
inum = 0, name =
```

解释：

- de 是目录项；
  - de 里面装着的 inum 是这个目录项对应的挂载位次；
  - char 数组是这个目录项的名字；

- while的 read 每次从 fd 里面读一个 dirent 大小的数据写到 de 里面；
- 然后 read 依次读取这个fd下的挂载目录，直到读完。
- 然后因为挂载的内容读完了，还剩下一堆无挂载的空闲空间；
  - *这些空间对应的 inum 是 0 就是没内容，所以就不断 continue。*
- 直到 read 彻底结束不再返回 sizeof(dirent)，while 条件不满足退出循环。

所以在 `ls.c` 中会把 inum 为 0 的跳过：

```c
while (read(fd, &de, sizeof(de)) == sizeof(de))
    {
        if (de.inum == 0)
            continue;
        printf("inum = %d, name = %s\n", de.inum, de.name);
    }
```

### 3.2 stat(path, &st)

和 `fstat(fd, &st)` 功能类似，只是参数为路径的字符串。

在 `ulib.c` 中：

```c
int
stat(const char *n, struct stat *st)
{
  int fd;
  int r;

  fd = open(n, O_RDONLY);
  if(fd < 0)
    return -1;
  r = fstat(fd, st);
  close(fd);
  return r;
}
```

可以看到就是用只读的方式打开了文件，调用了 fstat。

*这样的话就不用打开文件了。（乐）*

代码：

```c
#include "kernel/types.h"
#include "user/user.h"
#include "kernel/stat.h"
#include "kernel/fs.h"

int main()
{
    char path[512] = "/";

    struct stat st;

    // 获取文件信息
    stat(path, &st);

    printf("type: %d, inode number: %d, size: %d\n", st.type, st.ino, st.size);

    exit(0);
}
```

输出：

```shell
type: 1, inode number: 1, size: 1024
```

## 4 理解 ls.c

有了上面的铺垫，理解 ls.c 就是顺理成章的事情了。（其实我在写到这里是已经都懂了，下面梳理一下）

<img src="https://typora-1304621073.cos.ap-guangzhou.myqcloud.com/typora/%E7%90%86%E8%A7%A3%E4%BA%86%E4%B8%80%E5%88%87.png" alt="理解了一切" style="zoom:50%;" />

main 函数很好理解，就是在没有参数时，即 `ls` 理解为 `ls .`，输入了参数那就处理参数。

```c
int main(int argc, char *argv[])
{
  int i;

  if (argc < 2)
  {
    ls(".");
    exit(0);
  }
  for (i = 1; i < argc; i++)
    ls(argv[i]);
  exit(0);
}
```

然后是 ls 函数，我会直接在里面加上注释。

```c
void ls(char *path)
{

  char buf[512], *p; // 完整目录存储的字符数组（大概），p 是用来操作这个数组的指针
  int fd;
  struct dirent de; // 目录项
  struct stat st;   // 存储文件信息的结构体

  // 尝试按照路径打开文件
  if ((fd = open(path, 0)) < 0)
  {
    // 打不开就算了
    fprintf(2, "ls: cannot open %s\n", path);
    return;
  }

  // 尝试查看文件信息
  if (fstat(fd, &st) < 0)
  {
    // 查不到就算了
    fprintf(2, "ls: cannot stat %s\n", path);
    close(fd);
    return;
  }

  switch (st.type)
  {
  case T_FILE:
    // 文件
    printf("%s %d %d %l\n", fmtname(path), st.type, st.ino, st.size);
    break;

  case T_DIR:
    // 目录

    if (strlen(path) + 1 + DIRSIZ + 1 > sizeof buf)
    {
      // path 目录的基础上查看目录里的文件还要加上 '/' + 一个目录项 + '/' （大概）
      // 加上这就超出 512 了就说明 path 太长了
      printf("ls: path too long\n");
      break;
    }

    // 先复制一份 path 到 buf
    strcpy(buf, path);
    // 操作指针 p 也指向字符串的末尾
    p = buf + strlen(buf);
    // 补上一个 / 然后右移一位
    *p++ = '/';

    // 开始从 fd 里读目录项
    while (read(fd, &de, sizeof(de)) == sizeof(de))
    {
      // 没内容就跳过
      if (de.inum == 0)
        continue;

      // 有内容就把目录项名字的字符串复制到 buf 中（通过指针 p）
      // 可以发现，复制好像把空格也复制进去了
      memmove(p, de.name, DIRSIZ);
      p[DIRSIZ] = 0;

      // 那么这样就是一个新的路径 buf，用 stat 读取信息
      if (stat(buf, &st) < 0)
      {
        // 读不到就走
        printf("ls: cannot stat %s\n", buf);
        continue;
      }
      // 可以读就打印出来
      // fmtname(buf) 是打印出文件本来的名字，去掉了多余的路径
      printf("%s %d %d %d\n", fmtname(buf), st.type, st.ino, st.size);
    }
    break;
  }
  close(fd);
}
```

fmtname 函数就简单了，单纯就是返回文件本来的名字，去掉了多余的路径。

硬要说的话，它一定会返回一个长度为 DIRSIZ + 1 的字符串，即使文件名没那么长，它会在后面加上空格。

这样就对齐了，如下，可以数出就是 15 个字符。

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
fstat          2 31 22912
console        3 32 0
```

然后也可以试试直接输出 buf，就可以更直观的体会到区别和函数的作用了。

若改为：

```c
printf("%s %d %d %d\n", buf, st.type, st.ino, st.size);
```

则输出为：

```shell
$ ls
./. 1 1 1024
./.. 1 1 1024
./README 2 2 2059
./xargstest.sh 2 3 93
./cat 2 4 23976
./echo 2 5 22912
./forktest 2 6 13176
./grep 2 7 27328
./init 2 8 23904
./kill 2 9 22776
./ln 2 10 22728
./ls 2 11 26200
./mkdir 2 12 22880
./rm 2 13 22864
./sh 2 14 41752
./stressfs 2 15 23880
./usertests 2 16 147512
./grind 2 17 37992
./wc 2 18 25112
./zombie 2 19 22272
./sleep 2 20 22832
./copy 2 21 22496
./open 2 22 22360
./fork 2 23 22520
./exec 2 24 22488
./forkexec 2 25 23104
./redirect 2 26 23096
./pipe 2 27 22920
./pingpong 2 28 23232
./primes 2 29 24376
./pipe_read 2 30 23288
./fstat 2 31 22912
./console 3 32 0
```

## 5 END

总算讲完了，希望看完能对 `ls.c` 了解深刻些。
