# MIT 6.S081 - Lab Lock Memory allocator 测试

建议写完第一个 part 再来看，是我的测试过程，比较好笑，也比较长。

## 最初的正确（kfree、kalloc 对应，偷按顺序偷）

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

显然不可以每次 kalloc 都从 cpu0 的 freelist 开始拿，应该先去找自己的 freelist，不然竞争会很多。

### 随机完全体（kfree、kalloc 都是随机的）

既不对应，也不能从一个固定的位置去拿，应该怎么办？只能是随机了。

同时猜想：

在 kalloc 和 kfree 的时候足够随机就能避免竞争？大家的行为都会很随机，那这样竞争的概率不会很小吗？即使有那么多个 kalloc 和 kfree 同时发生，但是由于他们访问的 kmems 不同，所以不会产生很多竞争。

在这种情况下，我们可以初步预测：

- 开始 init 时，内存页被较均匀的分到了所有 freelist 的中。
- 在进程需要 kalloc 时，kalloc.c 从随机的一个 freelist 中为他找到一个空页。
- 在进程需要 kfree 时，kalloc.c 找到随机的一个 freelist 放入进程释放的页。

#### 产生随机数

发现于 `user/grind.c`。

线性同余发生器：给出了产生随机数的例子。

https://gist.github.com/xuanhao44/a3017d5eb609e44262e8fd0b073aef35.pibb

#### 某种想法：模数设置为实际运行的 CPU 个数

随机的模数设置为实际运行的 CPU 个数（想想这个是否合理）。

想在 xv6 中获取实际运行的 CPU 个数是困难的，只能从宏导入：

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

test1 没过，test2 根本无法开始。

usertests sbrkmuch、usertests 也都卡住了。

**完全不知道错在哪里**。只能用**控制变量**找一下问题。

### 随机 1（kfree、kalloc 对应，偷随机偷）

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

### 随机 2（kalloc 对应，kfree 随机）

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

usertests sbrkmuch、usertests 过了。

说明 kfree 的时候也要先把空页放回自己的 freelist，不然导致的竞争也很多。

## 随机 3（kalloc 第一次随机，偷对应偷，kfree 随机）

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

usertests sbrkmuch、usertests 过了。

竞争还是很多，kalloc 还是应该先尝试自己的 freelist。

## 阶段总结

|                       测试                        | test1 | test2 | usertests sbrkmuch | usertests | 错误原因（若有） |
| :-----------------------------------------------: | :---: | :---: | :----------------: | :-------: | :--------------: |
|          kfree、kalloc 对应，偷按顺序偷           | PASS  | PASS  |        PASS        |   PASS    |        -         |
|             kalloc 每次都从 0 开始拿              | FAIL  | PASS  |        PASS        |   PASS    |     竞争太多     |
|      随机完全体（kfree、kalloc 都是 随机的）      | FAIL  | STUCK |       STUCK        |   STUCK   |   窃取逻辑错误   |
|      随机 1（kfree、kalloc 对应，偷随机偷）       | PASS  | STUCK |       STUCK        |   STUCK   |   窃取逻辑错误   |
|         随机 2（kalloc 对应，kfree 随机）         | FAIL  | PASS  |        PASS        |   PASS    |     竞争太多     |
| 随机 3（kalloc 第一次随机，偷对应偷，kfree 随机） | FAIL  | PASS  |        PASS        |   PASS    |     竞争太多     |

这说明，kmems 和 cpuid 一一对应的设计是很正确的。不管是 kfree 还是 kalloc，每次都优先从自己的 freelist 开始取或者还，这样减少了很多的竞争。

不对应，尝试避免获取 cpuid 的设计都是有缺陷的。

通过这一系列测试，侧面说明了 cpuid 对应设计的优点。

但是仍然不能解决我的一个问题：

> 在 kalloc 和 kfree 的时候足够随机就能避免竞争？大家的行为都会很随机，那这样竞争的概率不会很小吗？

我仍然觉得这个猜想是有道理的，只不过这里 kmems 的数量太少了，不足以支持随机以消除竞争的意图。

## 扩大 kmems 数量

为什么 kmems 的数量一定要和实际 CPU 数量挂钩呢？这不太合理。

在随机 3 的基础上修改。

https://gist.github.com/xuanhao44/39558aea2ab21c842454cdbba05c2ea3.pibb

经测试发现：

- 在 CPU 数为 8 的情况下，KMEMS 高于某个值后，在编译 xv6 时就会 panic，该值为 345；成功编译的情况下，KMEMS 高于某个值后，测试程序 usertests sbrkmuch 也会 panic，该值为 300。

- 上述上限值与 CPU 数无关。

  KMEMS 越大，tot 值越小，这符合我们的判断。

- kalloctest test1 如果输出太多会崩掉，故去掉大量输出，下面是原因和调整方法。

user/kalloctest.c 片段：

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

这样输出就少了很多，当然 tot 的统计还是正常的，所以不影响最后的判断。

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

usertests sbrkmuch、usertests 过了。

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

usertests sbrkmuch、usertests 过了。

可以看到即使是这样努力改进，最后也只能把 tot 降低到这个程度。

## 结论 && 大佬指点

- 程序分配内存是具有局部性的。这样的话基于 cpuid 对应的做法才具有合理性。
- 基于 cpuid 的窃取做法是建立在窃取这件事发生概率低之上的，或者说两个进程抢内存的概率较小，这个概率是由实际程序的行为统计来的；在此之上对应 cpuid 的做法可以去除不需要窃取的情况下的锁争用；而散列的做法没法避免这个部分，只是尽量减少了竞争。
- 大哈希桶这种做法在系统内存紧缺的时候估计就比较糟糕。如 KMEMS = 345 时 usertests sbrkmuch 会崩掉。

## 设计和局部性的关系

解释一下上面的第一个结论。

### 思考实际的过程

基于 cpuid 对应的设计：

init 的时候，通过 free 为每个 CPU 都提供了一个 page 数较为均等的 freelist。

后续在使用的时候，进程（or CPU）申请了很多内存，那么就是先从自己这预分配有一定量 free page 的 freelist 取；之后释放，也是释放到自己的 freelist 中。

那么进一步考虑”连续申请并释放”这个内存使用的**局部性**。

> 程序在短时间内的行为模式是倾向于相同的。对于一个短时间内申请了大量内存，然后又释放了大量内存的程序，我们可以期望，他在接下来的时间内，做相同事情的概率很大。

拥有自己的 freelist 这件事就是非常合理的——考虑到局部性，之后仍然是从自己的 freelist 取和还；能满足上一次取和还的 freelist，这一次很大可能能继续满足，即基本上没有竞争的可能（如果不够，那么就窃取，这样最后 freelist 里的 free page 量就更多了，下下一次的行为就更好满足了；当然，在预分配了 freelist 后，窃取的概率也是很低的）。**你可以认为这样的设计利用了局部性。**

kalloc、kfree 随机设计：

init 的时候，所有的 free page 被较为均匀的分配到 freelist 中，而并不是 CPU 的“预备” freelist，可以说 CPU 并不预先拥有有一定量 free page 的 freelist 的了。也就是说，进程在申请内存的时候，总是去 kmems[KMEMS] 中去随机的去取；释放内存的时候，总是去 kmems[KMEMS] 中去随机的释放。

进一步，根据局部性，应该为“连续申请并释放”这个事件的再次发生提供便利，但是这种随机的设计却并没有提供着这种便利：不管是第一次，第二次，还是第 N 次，都是在 kmems[KMEMS] 中随机取放——这是很随机的，无记忆性的，没有根据进程之前的行为进行预测或者优化的。**你可以认为这样的设计并没有利用局部性。**

### 从某个称作“局面”的东西去考虑

基于 cpuid 对应的设计：

局面很清楚（因为 freelist 很方便列举）。

例如（仅举例说明，不代表真实情况）：

|  id  | freelist 内 page 数量 |
| :--: | :-------------------: |
|  0   |          300          |
|  1   |          356          |
|  2   |          600          |
|  3   |          234          |
|  4   |          360          |
|  5   |          323          |
|  6   |          540          |
|  7   |          233          |

局面就是各个对应的 freelist 的 page 数量。

从同一个初始局面开始，不同的 CPU 完成一样的行为（申请内存和释放内存）**，得到的局面是不一样的**。并且，不同 CPU 完成这样的行为之后，**局面就向着下一次更方便的执行这个行为变化**。

kalloc、kfree 随机设计：

freelist 太多，局面不好表示。故局面用概率分布密度函数去描述概括。

例如（仅举例说明，不代表真实情况）：

| freelist 内 page 数量 | 处于该区间的 freelist 占所有 freelist 的百分比 |
| :-------------------: | :--------------------------------------------: |
|        1 - 100        |                       3%                       |
|        100-200        |                      10%                       |
|       200 - 300       |                      20%                       |
|       300 - 400       |                      40%                       |
|          ...          |                      ...                       |

随机变量 x 是 freelist 中 page 数量，f(x) 是是这个数量的 freelist 占所有 freelist 的百分比。
$$
P(a<x \le b)=\int_{a}^{b}f(x)\mathrm{d}x
$$
从同一个初始局面 $f_0 (x)$ 开始，不同的 CPU 完成一样的行为（申请内存和释放内存），**得到的局面是一样的，都变化为 $f_{new} (x)$，对局面的改变是相同的**。也就是说这种设计不会被某个特定 CPU 影响，那么自然局面也不会向着方便某个 CPU 去执行行为去变化——并没有这样的趋势。

