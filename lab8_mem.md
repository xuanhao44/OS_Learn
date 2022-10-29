# MIT 6.S081 - Lab Lock Memory allocator

*PS：所有 kmem 在不需要仔细区分的情况下都被称为 freelist。*

## 1 内存情况

在 xv6 中，设置了总量为 128MB 的物理内存空间：从 KERNBASE = 0x80000000，到 PHYSTOP = KERNBASE + 128\*1024\*1024。

能够分配的物理内存是从 kernel 结束地址（end）到 PHYSTOP，也即内核代码段（kernel text）和数据段（kernel data）会占用一部分空间，导致剩余的空闲空间比 128MB 要小。

```c
extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

...(many)

void kinit()
{
  for (int i = 0; i < NCPU; i++)
    initlock(&kmems[i].lock, kmemsIDs[i]);

  freerange(end, (void *)PHYSTOP);
}
```

## 2 kfree 和 kalloc 的操作

kfree：把回收来的 page 放到 freelist 的开头。

```c
r->next = kmem.freelist;
kmem.freelist = r;
```

kalloc：把 freelist 开头的 page 取出 freelist。

```c
r = kmem.freelist;
if (r)
  kmem.freelist = r->next;
```

## 3 优化方法

修改空闲内存链表就是 freelist，现在我们要减少锁的争抢， **使每个 CPU 核使用独立的链表** ，而不是现在的共享链表。这样等分，就不会让所有的 CPU 争抢一个空闲区域。**注意**：每个空闲物理页只能存在于一个 freelist 中。 

## 4 优化实现

- 可以使用 `kernel/param.h` 中的宏 NCPU，其表示 CPU 的数目。

```c
struct kmem
{
  struct spinlock lock;
  struct run *freelist;
} kmems[NCPU];
// NCPU: from kernel/param.h; 8 - maximum number of CPUs
```

- 使用 cpuid() 函数会返回当前 CPU 的 id 号（范围0 ~ NCPU - 1）。在调用 cpuid() 并使用其返回值的过程中需要关闭中断。使用 push_off() 可以关闭中断，使用 pop_off() 可以打开中断。

使用的时候大致如：

```c
push_off();
{
  int id = cpuid();
  ...(code)
}
pop_off();
```

里面的代码尽量不包含与 cpuid 无关的代码。id 不在其他地方有效用。

- 请使用 initlock() 初始化锁，并 **要求锁名字以 kmem 开头** 。

```c
void kinit()
{
  for (int i = 0; i < NCPU; i++)
    initlock(&kmems[i].lock, ”kmems“);

  freerange(end, (void *)PHYSTOP);
}
```

freelist 变多了，要初始化的锁也多了。（snprintf 不太会用，就没管了，总之是输出一些锁的信息的）

- 使用 freerange 为所有运行 freerange 的 CPU 分配空闲的内存。

```c
void kfree(void *pa)
{
  struct run *r;

  if (((uint64)pa % PGSIZE) != 0 || (char *)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run *)pa;

  push_off();
  {
    int id = cpuid();
    acquire(&kmems[id].lock);
    {
      r->next = kmems[id].freelist;
      kmems[id].freelist = r;
    }
    release(&kmems[id].lock);
  }
  pop_off();
}
```

最开始的想法是平分每个物理页面，roll 随机数来分配给对应的 CPU。不过这样的话就要把 init 和之后的 free 分离成两个函数了。开始是 roll 随机数来分配，不过 free 还是要还到当前 CPU。

*引用于 xv6 book 9.3（book-riscv-rev1，2020 版本）*

> Another is the started variable in main.c (kernel/main.c:7), used to prevent other CPUs from running until CPU zero has finished initializing xv6; the volatile ensures that the compiler actually generates load and store instructions.

实际上不用这么做，因为只有 cpu0 会执行 kinit 以及 freerange，一开始把所有物理内存分给 cpu0 即可。

这样别的 CPU 执行 kalloc 时会从cpu0 “偷取” 物理内存，kfree 时直接还给这个 CPU 的 freelist，即谁分配就退还给谁。而且不存在的 CPU 对应的 freelist 一直为空。

- 最后是 kalloc

先从 CPU 自己对应的 freelist 开始找，然后如果不够再去其他的 CPU 的 freelist 找，也就是窃取。另外记得中断和加锁。

先找自己的：

```c
// check id’s(self) freelist first
acquire(&kmems[id].lock);
{
  r = kmems[id].freelist;
  if (r)
    kmems[id].freelist = r->next;
}
release(&kmems[id].lock);
```

找不到就去窃取：

```c
// r = null, then steal from others' freelist
for (int i = 0; (!r) && (i < NCPU); i++)
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
```

在 freelist 不为空的情况下不会产生锁的争用，因为只用到了本 CPU 的锁；但是如果需要偷取内存，就可能产生锁的争用。但由于 “偷取时对应 CPU 正好需要 kalloc” 这件事概率不高，并且 acquire 和 release 之间距离比较近，所以产生争用的可能性不大，因此 kalloctest 时输出的 tot 为 0。

## 5 最终实现

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

struct run
{
  struct run *next;
};

struct kmem
{
  struct spinlock lock;
  struct run *freelist;
} kmems[NCPU];
// NCPU: from kernel/param.h; 8 - maximum number of CPUs

void kinit()
{
  for (int i = 0; i < NCPU; i++)
    initlock(&kmems[i].lock, ”kmems“);

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

  push_off();
  {
    int id = cpuid();
    acquire(&kmems[id].lock);
    {
      r->next = kmems[id].freelist;
      kmems[id].freelist = r;
    }
    release(&kmems[id].lock);
  }
  pop_off();
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *kalloc(void)
{
  struct run *r;

  push_off();
  {
    int id = cpuid();

    // check id’s(self) freelist first
    acquire(&kmems[id].lock);
    {
      r = kmems[id].freelist;
      if (r)
        kmems[id].freelist = r->next;
    }
    release(&kmems[id].lock);

    // r = null, then steal from others' freelist
    for (int i = 0; (!r) && (i < NCPU); i++)
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
  }
  pop_off();

  if (r)
    memset((char *)r, 5, PGSIZE); // fill with junk
  return (void *)r;
}
```

## 评测

1. `kalloctest` 程序有 test1 和 test2。
   1. test1：主要是检测锁争用。
   2. test2：测试所有空闲内存的获取和释放。
2. `usertests sbrkmuch` 测试申请巨量内存下，系统运行是否正常。
3. `usertests`，确保其能能够全部通过。

只有 test1 是在测锁争用情况，其他测试都是在测试正确性或者性能（这比较合理，毕竟优化的前提是正确）。

```shell
$ kalloctest 
start test1
test1 results:
--- lock kmem/bcache stats
lock: kmems: #fetch-and-add 0 #acquire() 72052
lock: kmems: #fetch-and-add 0 #acquire() 183580
lock: kmems: #fetch-and-add 0 #acquire() 177431
lock: bcache: #fetch-and-add 0 #acquire() 1288
--- top 5 contended locks:
lock: proc: #fetch-and-add 43092 #acquire() 192259
lock: proc: #fetch-and-add 15382 #acquire() 192262
lock: virtio_disk: #fetch-and-add 13029 #acquire() 114
lock: proc: #fetch-and-add 12459 #acquire() 192246
lock: proc: #fetch-and-add 11162 #acquire() 192246
tot= 0
test1 OK
start test2
total free number of pages: 32499 (out of 32768)
.....
test2 OK
```

你可以看到，tot = 0，也就是说基本没有锁争用。

usertests sbrkmuch 和 usertest 输出太多，就不记录了。