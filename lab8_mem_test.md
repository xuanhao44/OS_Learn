# MIT 6.S081 - Lab Lock Memory allocator 测试

建议写完第一个 part 再来看，是我的测试过程，比较好笑，也比较长。

## 最初的正确（free、kalloc 对应，偷按顺序偷）

https://gist.github.com/xuanhao44/83f8f7344a0a04f2d4b611457dbeb2fe.pibb

```shell
$ kalloctest
start test1
test1 results:
--- lock kmem/bcache stats
lock: kmems0: #fetch-and-add 0 #acquire() 56871
lock: kmems1: #fetch-and-add 0 #acquire() 197501
lock: kmems2: #fetch-and-add 0 #acquire() 178695
lock: bcache: #fetch-and-add 0 #acquire() 1272
--- top 5 contended locks:
lock: proc: #fetch-and-add 31546 #acquire() 203227
lock: proc: #fetch-and-add 14772 #acquire() 203357
lock: proc: #fetch-and-add 12270 #acquire() 203347
lock: virtio_disk: #fetch-and-add 7386 #acquire() 114
lock: proc: #fetch-and-add 6363 #acquire() 203212
tot= 0
test1 OK
start test2
total free number of pages: 32499 (out of 32768)
.....
test2 OK
```

kalloctest test1、test2 过了。

usertests sbrkmuch、usertests 过了，输出太多，略。

## 某个思路：不对应，不获取 id（不中断）

**kmems 和 cpuid 一定要对应吗？一定要获取 id，非要中断一下吗？**

可以尝试不对应，不获取 id，不中断。

### kalloc 每次都从 cpu0 的 freelist 开始拿

也许没必要每次都从自己的 freelist 去拿？

https://gist.github.com/xuanhao44/579527186405ea4e17bab97f5baeb961.pibb

```shell
$ kalloctest
start test1
test1 results:
--- lock kmem/bcache stats
lock: kmems0: #fetch-and-add 20955 #acquire() 265415
lock: kmems1: #fetch-and-add 22684 #acquire() 222352
lock: kmems2: #fetch-and-add 0 #acquire() 128055
lock: bcache: #fetch-and-add 0 #acquire() 1272
--- top 5 contended locks:
lock: proc: #fetch-and-add 43883 #acquire() 202636
lock: kmems1: #fetch-and-add 22684 #acquire() 222352
lock: kmems0: #fetch-and-add 20955 #acquire() 265415
lock: virtio_disk: #fetch-and-add 10462 #acquire() 114
lock: proc: #fetch-and-add 6403 #acquire() 202724
tot= 43639
test1 FAIL
start test2
total free number of pages: 32499 (out of 32768)
.....
test2 OK
```

kalloctest test1 没过，test2 过了。

usertests sbrkmuch、usertests 过了，输出太多，略。

显然不可以只从 0 开始拿，应该先去找自己的，不然竞争会很多。

### 随机完全体（free、kalloc 都是随机的）

既不对应，也不能从一个固定的位置去拿，应该怎么办？只能是随机了。

同时猜想：在 kalloc 和 free 的时候足够随机就能避免竞争？大家的行为都会很随机，那这样竞争的概率不会很小吗？

#### 产生随机数

发现于 `user/grind.c`。

线性同余发生器：给出了产生随机数的例子。

https://gist.github.com/xuanhao44/a3017d5eb609e44262e8fd0b073aef35.pibb

#### 获取实际运行 CPU 数

随机的模数可不能设置为 NCPU，必须是实际运行的 CPU 个数。

想在 xv6 中获取际运行的 CPU 个数是困难的，只能从宏导入：

**在 makefile 中添加参数，在 main.c 设置 CPU_NUM 获取，在 kalloc.c 中用 extern 使用。**

makefile 中加一句：

```makefile
CFLAGS += -DCPUS=${CPUS}
```

main.c 中：（截取部分）

```c
volatile static int started = 0;

int CPU_NUM = 0;

// start() jumps here in supervisor mode on all CPUs.
void
main()
{
  if(cpuid() == 0){
    consoleinit();
#if defined(LAB_PGTBL) || defined(LAB_LOCK)
    statsinit();
#endif
    printfinit();
    printf("\n");
    printf("xv6 kernel is booting\n");
    #if defined(CPUS)
      CPU_NUM = CPUS;
      printf("CPU_NUM = %d\n", CPU_NUM);
    #endif
    printf("\n");
```

kalloc.c

```c
extern int CPU_NUM; // from main.c
                    // defined by makefile
```

#### 实现

https://gist.github.com/xuanhao44/9e1026154075679191315aaeef6993f2.pibb

```shell
$ kalloctest
start test1
test1 results:
--- lock kmem/bcache stats
lock: kmems0: #fetch-and-add 5028 #acquire() 144118
lock: kmems1: #fetch-and-add 7542 #acquire() 144469
lock: kmems2: #fetch-and-add 10568 #acquire() 144429
lock: bcache: #fetch-and-add 0 #acquire() 1288
--- top 5 contended locks:
lock: proc: #fetch-and-add 46623 #acquire() 174894
lock: proc: #fetch-and-add 21521 #acquire() 174979
lock: virtio_disk: #fetch-and-add 18129 #acquire() 114
lock: proc: #fetch-and-add 12107 #acquire() 174981
lock: kmems2: #fetch-and-add 10568 #acquire() 144429
tot= 23138
test1 FAIL
```

test1 没过，跑不到 test2，笑死。

usertests sbrkmuch、usertests 也都卡住了。

**完全不知道错在哪里**。只能用**控制变量**找一下问题。

### 随机 1（free、kalloc 对应，偷随机偷）

https://gist.github.com/xuanhao44/3faf3c5545801acf7310e141af08f0a4.pibb

```shell
$ kalloctest
start test1
test1 results:
--- lock kmem/bcache stats
lock: kmems0: #fetch-and-add 0 #acquire() 80634
lock: kmems1: #fetch-and-add 0 #acquire() 182048
lock: kmems2: #fetch-and-add 0 #acquire() 170371
lock: bcache: #fetch-and-add 0 #acquire() 356
--- top 5 contended locks:
lock: proc: #fetch-and-add 35693 #acquire() 210328
lock: proc: #fetch-and-add 21585 #acquire() 210402
lock: proc: #fetch-and-add 15159 #acquire() 210402
lock: virtio_disk: #fetch-and-add 5983 #acquire() 57
lock: proc: #fetch-and-add 3569 #acquire() 210314
tot= 0
test1 OK
```

test1 过了，但跑不到 test2。

usertests sbrkmuch、usertests 也都卡住了。

错误也许是显然的。

```c
while (!r)
  {
    int i = randN();
    acquire(&kmems[i].lock);
    {
      r = kmems[i].freelist;
      if (r)
        kmems[i].freelist = r->next;
    }
    release(&kmems[i].lock);
  }
```

假设一种情况：如果使用的内存过多，各个 kmems 的 freelist 确实空了。

那么，这段代码并不会知道这个情况，它只知道我现在随机到的这个 freelist 没找到空页，那我就再随即一次就好了——可是每个 freelist 确实空了，再怎么随机也随机不到！

此外，如果出现了某个 kmems 的 freelist 空了的情况，而 rand 总是会有近似于 1/CPU_NUM 的概率选到这个 kmems，这样当然是不必要的，造成很多浪费。

如果想要修改正确，只能按照最开始的那样遍历一次。

### 随机 2（kalloc 对应，free 随机）

https://gist.github.com/xuanhao44/4efc88533957be3a78c7efb6d82e52ae.pibb

```shell
$ kalloctest
start test1
test1 results:
--- lock kmem/bcache stats
lock: kmems0: #fetch-and-add 2182 #acquire() 123617
lock: kmems1: #fetch-and-add 4118 #acquire() 156831
lock: kmems2: #fetch-and-add 3180 #acquire() 156460
lock: bcache: #fetch-and-add 0 #acquire() 1288
--- top 5 contended locks:
lock: proc: #fetch-and-add 21427 #acquire() 241841
lock: virtio_disk: #fetch-and-add 7840 #acquire() 114
lock: proc: #fetch-and-add 7123 #acquire() 241918
lock: proc: #fetch-and-add 4878 #acquire() 241865
lock: proc: #fetch-and-add 4850 #acquire() 241923
tot= 9480
test1 FAIL
start test2
total free number of pages: 32499 (out of 32768)
.....
test2 OK
```

kalloctest test1 没过，test2 过了。

usertests sbrkmuch、usertests 过了，输出太多，略。

说明 free 的时候也要先把空页放回自己的 freelist，不然导致的竞争也很多。

## 随机 3（kalloc 第一次随机，偷对应偷，free 随机）

在随机 2 的基础上更改。

https://gist.github.com/xuanhao44/5ce53a381b6b0a76220c9d8945bbf1e7.pibb

```shell
$ kalloctest
start test1
test1 results:
--- lock kmem/bcache stats
lock: kmems0: #fetch-and-add 5394 #acquire() 144110
lock: kmems1: #fetch-and-add 20061 #acquire() 144483
lock: kmems2: #fetch-and-add 6533 #acquire() 144423
lock: bcache: #fetch-and-add 0 #acquire() 1288
--- top 5 contended locks:
lock: proc: #fetch-and-add 41651 #acquire() 189947
lock: kmems1: #fetch-and-add 20061 #acquire() 144483
lock: proc: #fetch-and-add 19724 #acquire() 190049
lock: proc: #fetch-and-add 10150 #acquire() 190053
lock: virtio_disk: #fetch-and-add 7697 #acquire() 114
tot= 31988
test1 FAIL
start test2
total free number of pages: 32499 (out of 32768)
.....
test2 OK
```

kalloctest test1 没过，test2 过了。

usertests sbrkmuch、usertests 过了，输出太多，略。

竞争还是很多，kalloc 还是应该先尝试自己的 freelist。

## 阶段总结

|                       测试                       | test1 | test2 | usertests sbrkmuch | usertests | 错误原因（若有） |
| :----------------------------------------------: | :---: | :---: | :----------------: | :-------: | :--------------: |
|          free、kalloc 对应，偷按顺序偷           | PASS  | PASS  |        PASS        |   PASS    |        -         |
|             kalloc 每次都从 0 开始拿             | FAIL  | PASS  |        PASS        |   PASS    |     竞争太多     |
|      随机完全体（free、kalloc 都是 随机的）      | FAIL  | STUCK |       STUCK        |   STUCK   |   窃取逻辑错误   |
|      随机 1（free、kalloc 对应，偷随机偷）       | PASS  | STUCK |       STUCK        |   STUCK   |   窃取逻辑错误   |
|         随机 2（kalloc 对应，free 随机）         | FAIL  | PASS  |        PASS        |   PASS    |     竞争太多     |
| 随机 3（kalloc 第一次随机，偷对应偷，free 随机） | FAIL  | PASS  |        PASS        |   PASS    |     竞争太多     |

这说明，kmems 和 cpuid 一一对应的设计是很正确的。不对应，尝试避免获取 id 的设计都是有缺陷的。

通过这一系列测试，也侧面说明了 cpuid 对应设计的优点。

但是仍然不能解决我的一个问题：

> 在 kalloc 和 free 的时候足够随机就能避免竞争？大家的行为都会很随机，那这样竞争的概率不会很小吗？

我仍然觉得这个猜想是有道理的，只不过这里 kmems 的数量太少了，不足以支持随机以消除竞争的意图。

## 扩大数量

为什么 kmems 的数量一定要和实际 CPU 数量挂钩呢？这样是不合理的。

在随机 3 的基础上修改。

https://gist.github.com/xuanhao44/39558aea2ab21c842454cdbba05c2ea3.pibb

经测试发现：

在 CPU 数为 8 的情况下，KMEMS = 345 为不在编译时就 panic 的最大数。

KMEMS = 300 约为不在编译时就 panic 且 usertests sbrkmuch 不崩的最大数。

经进一步测试发现上述上限值与 CPU 数无关。

同时，数字越大，tot 值越小，这符合我们的判断。

另外 kalloctest test1 如果输出太多会崩掉，故去掉大量输出。

kalloctest 片段：

```c
#define NCHILD 2
#define N 100000
#define SZ 4096

void test1(void);
void test2(void);
char buf[SZ];

int
main(int argc, char *argv[])
{
  test1();
  test2();
  exit(0);
}

int ntas(int print)
{
  int n;
  char *c;

  if (statistics(buf, SZ) <= 0) {
    fprintf(2, "ntas: no stats\n");
  }
  c = strchr(buf, '=');
  n = atoi(c+2);
  if(print)
    printf("%s", buf);
  return n;
}
```

注意到 SZ 只有 4096，输出多起来就会出错。

于是把 kernel/spinlock.c:statslock() 的一个循环里输出 kmem.lock 的部分注释掉。

```c
for(int i = 0; i < NLOCK; i++) {
    if(locks[i] == 0)
      break;
    if(strncmp(locks[i]->name, "bcache", strlen("bcache")) == 0 ||
       strncmp(locks[i]->name, "kmem", strlen("kmem")) == 0) {
      tot += locks[i]->nts;
      // n += snprint_lock(buf +n, sz-n, locks[i]);
    }
  }
```

### CPU 数为 8，KMEMS = 300

```shell
$ kalloctest
start test1
test1 results:
--- lock kmem/bcache stats
--- top 5 contended locks:
lock: proc: #fetch-and-add 970212 #acquire() 816485
lock: proc: #fetch-and-add 917104 #acquire() 815842
lock: proc: #fetch-and-add 871519 #acquire() 815839
lock: proc: #fetch-and-add 830139 #acquire() 815844
lock: proc: #fetch-and-add 823397 #acquire() 815842
tot= 39588
test1 FAIL
start test2
total free number of pages: 32496 (out of 32768)
.....
test2 OK
```

usertests sbrkmuch、usertests 过了，输出太多，略。

### CPU 数为 3，KMEMS = 300

```shell
$ kalloctest
exec mkalloctest failed
$ kalloctest
start test1
test1 results:
--- lock kmem/bcache stats
--- top 5 contended locks:
lock: proc: #fetch-and-add 113530 #acquire() 196919
lock: proc: #fetch-and-add 30513 #acquire() 196916
lock: proc: #fetch-and-add 10455 #acquire() 196919
lock: proc: #fetch-and-add 7804 #acquire() 197003
lock: virtio_disk: #fetch-and-add 5729 #acquire() 57
tot= 4951
test1 FAIL
start test2
total free number of pages: 32496 (out of 32768)
.....
test2 OK
```

usertests sbrkmuch、usertests 过了，输出太多，略。

### 最后结论 && 大佬指点

- 程序分配内存是具有局部性的，某一个密集时段大量分配和释放内存，剩下时间没有任何内存分配。这样的话基于 cpuid 对应的做法才具有合理性。

- 基于 cpuid 的窃取做法是建立在窃取这件事发生概率低之上的，或者说两个进程抢内存的概率较小，这个概率是由实际程序的行为统计来的；在此之上对应 cpuid 的做法可以去除不需要窃取的情况下的锁争用；而散列的做法没法避免这个部分，只是尽量减少了竞争。
- 大哈希桶这种做法在系统内存紧缺的时候估计就比较糟糕。如 KMEMS = 345 时 usertests sbrkmuch 会崩掉。

