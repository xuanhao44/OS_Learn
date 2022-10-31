# MIT 6.S081 - Lab Lock Memory allocator - test

本篇是我的测试过程，建议写完第一个 part 再来看。

## 1 某个思路：随机

**freelist 和 cpuid 一定要对应起来吗？**

既不对应，也不能从一个固定的位置去拿，应该怎么办？只能是随机了。

同时猜想：是不是在 kalloc 和 kfree 的时候足够随机就能避免竞争？大家的行为都会很随机，那这样竞争的概率不会很小吗？那么即使有多个 kalloc 和 kfree 同时发生，但是由于他们访问的 freelist 不同，所以不会产生很多锁争用。

在这种情况下，可初步预测：

- 开始 init 时，内存页被较均匀的分到了所有 freelist 的中。
- 在进程需要 kalloc 时，kalloc.c 从随机的一个 freelist 中为他找到一个 free page。
- 在进程需要 kfree 时，kalloc.c 找到随机的一个 freelist 放入进程释放的 page。

## 2 初步考虑随机相关问题

### 2.1 产生随机数

线性同余发生器：给出了产生随机数的例子。发现于 `user/grind.c`。

```c
int do_rand(uint64 *ctx)
{
    long x = (*ctx % 0x7ffffffe) + 1;
    long hi = x / 127773;
    long lo = x % 127773;
    x = 16807 * lo - 2836 * hi;
    if (x < 0)
        x += 0x7fffffff;
    x--;
    *ctx = x;
    return x;
}

uint64 rand_next = 1;

int randN(void)
{
    return do_rand(&rand_next);
}
```

### 2.2 模数取决于 freelist 数量

产生的随机数很大，不是拿来直接用的。一般的做法是对其取模。

那么选什么当模数？模数取决于 freelist 的数量。设置为多少比较好呢？

#### 2.2.1 实际运行的 CPU 个数

尝试设置为实际运行的 CPU 个数。

想在 xv6 中获取实际运行的 CPU 个数是困难的，只能从宏导入：

**在 makefile 中添加参数，在 main.c 设置 CPU_NUM 获取，在 kalloc.c 中用 extern 使用。**

`makefile` 中加上：

```makefile
CFLAGS += -DCPUS=${CPUS}
```

`kernel/main.c` 中：（截取部分）

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

`kernel/kalloc.c` 中：

```c
extern int CPU_NUM; // from main.c
                    // defined by makefile
```

#### 2.2.2 设置为较大的数

为什么 freelist 的数量一定要和实际 CPU 数量挂钩呢？这不太合理。我们完全可以设置为一个较大的数。

在下面的实验中，我们将设置为 300。

```c
#define KMEMS 300

struct kmem
{
  struct spinlock lock;
  struct run *freelist;
} kmems[KMEMS];
```

*实际上设置多少挺玄学的，要考虑到很多问题。接下来会提到一部分，但也不完全。*

经测试发现：

- 在 CPU 数为 8 的情况下，KMEMS 高于某个值后，在编译 xv6 时就会 panic，该值为 315；
- 成功编译的情况下，KMEMS 高于某个值后，测试程序 `usertests sbrkmuch` 也会 panic，该值为 310；
- 上限值 315 和 310 与 CPU 数无关；不保证该值是准确值，不保证一直有效，仅供参考。
- `kalloctest` test1 如果输出太多会崩掉，故去掉大量输出，下面是原因和调整方法。

`user/kalloctest.c` 片段：

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

于是把 `kernel/spinlock.c:statslock()` 的一个循环里输出 `kmem.lock` 的部分注释掉。

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

## 3 从一个错误百出的程序开始

让我们以这个程序为基础，一步步开始 ”优化“ 吧。

```c
// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

int do_rand(uint64 *ctx)
{
  long x = (*ctx % 0x7ffffffe) + 1;
  long hi = x / 127773;
  long lo = x % 127773;
  x = 16807 * lo - 2836 * hi;
  if (x < 0)
    x += 0x7fffffff;
  x--;
  *ctx = x;
  return x;
}

uint64 rand_next = 1;

int randN(void)
{
  return do_rand(&rand_next);
}

struct run
{
  struct run *next;
};

#define KMEMS 300

struct kmem
{
  struct spinlock lock;
  struct run *freelist;
} kmems[KMEMS];

void kinit()
{
  for (int i = 0; i < KMEMS; i++)
    initlock(&kmems[i].lock, "kmems");

  freerange(end, (void *)PHYSTOP);
}

void freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char *)PGROUNDUP((uint64)pa_start);
  for (; p + PGSIZE <= (char *)pa_end; p += PGSIZE)
    kfree(p);
}

// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void kfree(void *pa)
{
  struct run *r;

  if (((uint64)pa % PGSIZE) != 0 || (char *)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run *)pa;

  int id = randN() % KMEMS;
  acquire(&kmems[id].lock);
  {
    r->next = kmems[id].freelist;
    kmems[id].freelist = r;
  }
  release(&kmems[id].lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;
  int id;
  do
  {
    id = randN() % KMEMS;
    acquire(&kmems[id].lock);
    {
      r = kmems[id].freelist;
      if (r)
        kmems[id].freelist = r->next;
    }
    release(&kmems[id].lock);
  } while (!r);

  if (r)
    memset((char *)r, 5, PGSIZE); // fill with junk

  return (void *)r;
}
```

测试结果：`kalloctest` test1 没过，test2 根本无法开始；`usertests sbrkmuch`、`usertests` 也都卡住了！

### 3.1 不可完全随机的 kalloc

错误也许是显然的。

```c
struct run *r;
int id;
do
{
  id = randN() % KMEMS;
  acquire(&kmems[id].lock);
  {
    r = kmems[id].freelist;
    if (r)
      kmems[id].freelist = r->next;
  }
  release(&kmems[id].lock);
} while (!r);
```

假设一种情况：使用的内存过多，各个 freelist 都没有多少 free page 了。

对应到这段代码，我们只是随机的选取了一个 freelist，然后去取它的 free page，如果没有的话就再次随机的选取一个 freelist。

然而各个 freelist 都没有多少 free page 了，故而随机选取一次，能找到 free page 的概率是很低的。

从整体上说，随着分配回收 free page 的进行，如果进程申请的内存比释放的 free page 多，所有 freelist 的 free page 是在不断减少的，随机选取一次，能找到 free page 的概率会降低；如果有 N 个 freelist 没有 free page 了，那么随机选取一次，能找到 free page 的概率就是 $(1 - N/{KMEMS})$。

既然不能完全随机，那么我们改变策略：

先随机选取一次；如果没有得到 free page，就遍历所有 freelist 去寻找 free page。

```c
void *
kalloc(void)
{
  struct run *r;

  int id = randN()  % KMEMS;
  acquire(&kmems[id].lock);
  {
    r = kmems[id].freelist;
    if (r)
      kmems[id].freelist = r->next;
  }
  release(&kmems[id].lock);

  // r = null, then steal from all freelist
  for (int i = 0; (!r) && (i < KMEMS); i++)
  {
    if (i == id)
      continue;

    acquire(&kmems[i].lock);
    {
      r = kmems[i].freelist;
      if (r)
        kmems[i].freelist = r->next;
    }
    release(&kmems[i].lock);
  }

  if (r)
    memset((char *)r, 5, PGSIZE); // fill with junk

  return (void *)r;
}
```

测试结果：修改得到的程序虽然不能过 test1，但是 test2、`usertests sbrkmuch`、`usertests` 可以通过。

### 3.2 随机是否起效

实际上，上面程序生成随机数 `randN() % KMEMS` 并没有起到应有的效果。进程共享着 `uint64 rand_next`，也就是说在多数时候，同时运行的 CPU 使用着相同的 `rand_next`，那么对于线性同余发生器来说得到的值就是相同的——这完全不是我们想要的。我们希望同时运行的 CPU 通过随机达成减少竞争，而这个想法根本没有被实现。

改进的方法是：为每一个 CPU 设置 `rand_next`，每一个 CPU 在获取随机数时需要使用自己的 `rand_next`。

```c
uint64 rand_next[NCPU] = {1};

int randN(void)
{
  return do_rand(&rand_next[cpuid()]);
}
```

*考虑一下为什么没有像平常那样关闭和打开中断：使用 cpuid 时，我们不在意 CPU 是否切换——这对随机数的生成并没有实质的影响。*

测试结果：修改得到的程序不能过 test1，test2、`usertests sbrkmuch`、`usertests` 可以通过。但是 test1 的 tot 值明显的变小了很多。就是从上万到了两位数，这是个很大的进步！

*tot 的值表现的很随机，在两位数到五位数之间波动。但是能到两位数了。*

### 3.3 考虑锁：double check

一个可能出现的情况是所有 freelist 中剩余的 free page 不多。在这种情况下，很多加锁的步骤都是无效的，开销很大，且增加了很多锁争用。

**采用 “不安全 check + 加锁 double check” 的处理模式：利用某些可以允许的不安全来换取缩小的锁范围，然后再使用额外的手段保证正确性。**

```c
void *
kalloc(void)
{
  struct run *r = (void *)0;

  int id = randN() % KMEMS;
  // unsafe check
  if (kmems[id].freelist)
  {
    // acquire lock
    acquire(&kmems[id].lock);
    // double check
    if (kmems[id].freelist)
    {
      r = kmems[id].freelist;
      kmems[id].freelist = r->next;
    }
    release(&kmems[id].lock);
  }

  // r = null, then steal from all freelist
  for (int i = 0; (!r) && (i < KMEMS); i++)
  {
    if (i == id)
      continue;

    if (kmems[i].freelist)
    {
      // acquire lock
      acquire(&kmems[i].lock);
      // double check
      if (kmems[i].freelist)
      {
        r = kmems[i].freelist;
        kmems[i].freelist = r->next;
      }
      release(&kmems[i].lock);
    }
  }

  if (r)
    memset((char *)r, 5, PGSIZE); // fill with junk

  return (void *)r;
}
```

测试结果：修改得到的程序不能过 test1，test2、`usertests sbrkmuch`、`usertests` 可以通过。进一步优化的效果并不是很明显，tot 的值表现的很随机，在两位数到五位数之间波动。

### 3.4 考虑更随机的遍历

每次都从 0 开始不够随机，我们可以从 id + 1 开始遍历，充分利用了上面的随机数。

```c
int j;
for (int i = 0; (!r) && (i < KMEMS); i++)
{
  j = (id + 1 + i) % KMEMS;
  // unsafe check
  if (kmems[j].freelist)
  {
    // acquire lock
    acquire(&kmems[j].lock);
    // double check
    if (kmems[j].freelist)
    {
      r = kmems[j].freelist;
      kmems[j].freelist = r->next;
    }
    release(&kmems[j].lock);
  }
}
```

测试结果：和 3.3 一样，修改得到的程序不能过 test1，test2、`usertests sbrkmuch`、`usertests` 可以通过。进一步优化的效果并不是很明显，tot 的值表现的很随机，在两位数到五位数之间波动。

### 4 最终优化结果

```c
// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

int do_rand(uint64 *ctx)
{
  long x = (*ctx % 0x7ffffffe) + 1;
  long hi = x / 127773;
  long lo = x % 127773;
  x = 16807 * lo - 2836 * hi;
  if (x < 0)
    x += 0x7fffffff;
  x--;
  *ctx = x;
  return x;
}

uint64 rand_next[NCPU] = {1};

int randN(void)
{
  return do_rand(&rand_next[cpuid()]);
}

struct run
{
  struct run *next;
};

#define KMEMS 300

struct kmem
{
  struct spinlock lock;
  struct run *freelist;
} kmems[KMEMS];

void kinit()
{
  for (int i = 0; i < KMEMS; i++)
    initlock(&kmems[i].lock, "kmems");

  freerange(end, (void *)PHYSTOP);
}

void freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char *)PGROUNDUP((uint64)pa_start);
  for (; p + PGSIZE <= (char *)pa_end; p += PGSIZE)
    kfree(p);
}

// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void kfree(void *pa)
{
  struct run *r;

  if (((uint64)pa % PGSIZE) != 0 || (char *)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run *)pa;

  int id = randN() % KMEMS;
  acquire(&kmems[id].lock);
  {
    r->next = kmems[id].freelist;
    kmems[id].freelist = r;
  }
  release(&kmems[id].lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r = (void *)0;

  int id = randN() % KMEMS;
  // unsafe check
  if (kmems[id].freelist)
  {
    // acquire lock
    acquire(&kmems[id].lock);
    // double check
    if (kmems[id].freelist)
    {
      r = kmems[id].freelist;
      kmems[id].freelist = r->next;
    }
    release(&kmems[id].lock);
  }

  // r = null, then steal from all freelist
  int j;
  for (int i = 0; (!r) && (i < KMEMS); i++)
  {
    j = (id + 1 + i) % KMEMS;
    // unsafe check
    if (kmems[j].freelist)
    {
      // acquire lock
      acquire(&kmems[j].lock);
      // double check
      if (kmems[j].freelist)
      {
        r = kmems[j].freelist;
        kmems[j].freelist = r->next;
      }
      release(&kmems[j].lock);
    }
  }

  if (r)
    memset((char *)r, 5, PGSIZE); // fill with junk

  return (void *)r;
}
```

测试结果：

修改得到的程序不能过 test1，test2、`usertests sbrkmuch`、`usertests` 可以通过。进一步优化的效果并不是很明显，tot 的值表现的很随机，在两位数到五位数之间波动。

```shell
$ kalloctest
start test1
test1 results:
--- lock kmem/bcache stats
--- top 5 contended locks:
lock: proc: #fetch-and-add 352726 #acquire() 798905
lock: proc: #fetch-and-add 282570 #acquire() 798882
lock: proc: #fetch-and-add 277342 #acquire() 798864
lock: proc: #fetch-and-add 276305 #acquire() 798866
lock: proc: #fetch-and-add 264624 #acquire() 798864
tot= 1875
test1 FAIL
start test2
total free number of pages: 32496 (out of 32768)
.....
test2 OK
```

`usertests` 输出太多，略。

## 5 总结

freelist 和 cpuid 对应的设计是很正确的。不管是 kfree 还是 kalloc，每次都优先从自己的 freelist 开始取或者还，这样减少了很多的锁争用。

kfree、kalloc 随机还和取 free page 的设计是有缺陷的。

通过这一系列测试，说明了 cpuid 对应 freelist 设计的优点。

再返回去看之前的问题：

> 是不是在 kalloc 和 kfree 的时候足够随机就能避免竞争？
>
> 大家的行为都会很随机，那这样竞争的概率不会很小吗？
>
> 那么即使有多个 kalloc 和 kfree 同时发生，但是由于他们访问的 freelist 不同，所以不会产生很多锁争用。

在 3.3 中，我们发现过多的 freelist 可能会让遍历的过程变得更漫长，以及更多的锁和锁争用。这是我们不希望的。我们本来希望通过增加 freelist 的数量来使得竞争概率减小，但是最后却造成了更多的锁争用。

大佬指点，从另外的角度分析：

- 程序分配内存是具有局部性的。这样的话基于 cpuid 对应的做法才具有合理性。
- 基于 cpuid 的窃取做法是建立在窃取这件事发生概率低之上的，或者说两个进程抢内存的概率较小，这个概率是由实际程序的行为统计来的；在此之上对应 cpuid 的做法可以去除不需要窃取的情况下的锁争用；而散列的做法没法避免这个部分，只是尽量减少了竞争。

## 6 局部性

解释一下上面用局部性来说明合理性的结论。

### 6.1 思考实际的过程

基于 cpuid 对应的设计：

init 的时候，通过 free 为每个 CPU 都提供了一个 page 数较为均等的 freelist。

后续在使用的时候，进程（or CPU）申请了很多内存，那么就是先从自己这预分配有一定量 free page 的 freelist 取；之后释放，也是释放到自己的 freelist 中。

那么进一步考虑”连续申请并释放”这个内存使用的**局部性**。

> 程序在短时间内的行为模式是倾向于相同的。对于一个短时间内申请了大量内存，然后又释放了大量内存的程序，我们可以期望，他在接下来的时间内，做相同事情的概率很大。

拥有自己的 freelist 这件事就是非常合理的——考虑到局部性，之后仍然是从自己的 freelist 取和还；能满足上一次取和还的 freelist，这一次很大可能能继续满足，即基本上没有竞争的可能（如果不够，那么就窃取，这样最后 freelist 里的 free page 量就更多了，下下一次的行为就更好满足了；当然，在预分配了 freelist 后，窃取的概率也是很低的）。**你可以认为这样的设计利用了局部性。**

kalloc、kfree 随机设计：

init 的时候，所有的 free page 被较为均匀的分配到 freelist 中，而并不是 CPU 的 “预备” freelist，可以说 CPU 并不预先拥有有一定量 free page 的 freelist 的了。也就是说，进程在申请内存的时候，总是去 `kmems[KMEMS]` 中去随机的去取；释放内存的时候，总是去 `kmems[KMEMS]` 中去随机的释放。

进一步，根据局部性，应该为“连续申请并释放”这个事件的再次发生提供便利，但是这种随机的设计却并没有提供着这种便利：不管是第一次，第二次，还是第 N 次，都是在 `kmems[KMEMS]` 中随机取放——这是很随机的，无记忆性的，没有根据进程之前的行为进行预测或者优化的。**你可以认为这样的设计并没有利用局部性。**

### 6.2 从某个称作“局面”的东西去考虑

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

