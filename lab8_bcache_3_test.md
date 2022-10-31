# MIT 6.S081 - Lab Lock Buffer cache (3) 哈希单项优化 - 死锁处理以及 test and set 的 xv6 实现

上一篇文章提到死锁的问题没有被处理。虽然不会导致评测不成功，但是我们还是想想办法把这个问题处理一下。

## 0 尝试解决死锁

来看在其他 bucket 查找的过程。

产生死锁的情况是：我们持有 `buk_key->lock`，然后去锁另一个 bucket，同时另一个进程持有这个 bucket 的锁，去锁 `buk_key`。

我们给出的处理方法是：遍历的时候检测 bucket 是否上锁，如果上锁，则不去对它进行上锁和之后查找的操作。

似乎不像是死锁的标准的处理方式，不管是预防还是避免和检测。因为这里我们直接没有了这个需求，我们不访问了，因为严格来说，访问并不是必要的——我们只是想找到一个 LRU 块。

- 我们首先尝试使用 `holding(&buk->lock)` 来判断。

```c
// Check whether this cpu is holding the lock.
// Interrupts must be off.
int holding(struct spinlock *lk)
{
  int r;
  r = (lk->locked && lk->cpu == mycpu());
  return r;
}
```

但是很快便发现了问题。它不能起到应有的效果。如果被锁上，那么 locked 的值为 1，这就够了。但它还附加了条件 `lk->cpu == mycpu()`，这是我们不需要的。我们不关心这个锁被哪个其他的进程锁住，当然当前进程是否获得这个锁也不需要判断，是显然的事情。

- 我们再选择使用 `buk->lock.locked` 来判断。

但是这样也不行，因为检查到获取锁之间是有时间差的，完全可能中间就被锁了。**我们需要把检查和上锁两个操作合起来变成原子的。这个操作被称为 test and set，可 `kernel/spinlock.c` 中没有这个函数，我们需要自己写一个 `try_acquire`。**

（错误实现样例）：

```c
  // we cannot find unused buf in the hash(blockno) bucket
  // search in other bucket
  for (buk = bcache.bucket; buk < bcache.bucket + NBUCKETS; buk++)
  {
    // do not search hash(blockno) bucket again
    if (buk == buk_key)
      continue;

    if (buk->lock.locked)
      continue;

    acquire(&buk->lock);
    for (b = buk->head.prev; b != &buk->head; b = b->prev)
    {
      if (b->refcnt == 0)
      {
        b->dev = dev;
        b->blockno = blockno;
        b->valid = 0;
        b->refcnt = 1;

        remove(b);
        add_head(&buk_key->head, b);

        release(&buk->lock);
        release(&buk_key->lock);
        acquiresleep(&b->lock);
        return b;
      }
    }
    release(&buk->lock);
  }
```

## 1 锁的实现

我们先来了解锁的实现。

本小节内容来自指导书 [xv-6中Lock的实现（选读）](http://hitsz-cslab.gitee.io/os-labs/lab3/part4/)。

更原始的源头是 xv6 book Locking 章节。

调用 `acquire()` 和 `release()` 来获取锁和释放锁。首先需要知道的是锁的 `acquire`（P）和 `release`（V）的实现并不是一件简单的事情。我们先来看一个最 naive 的实现：

```c
void acquire(struct spinlock *lk) // does not work!
{
  for (;;)
  {
    if (lk->locked == 0)
    {
      lk->locked = 1;
      break;
    }
  }
}
```

这个代码非常容易理解，如果 lk 被锁上了，那就继续循环，如果 lk 未被锁上，那就给它加锁并退出。但这个实现是有缺陷的，让我们试想一下以下场景：

- lk 一开始没有被锁上，即 `lk->locked = 0`;
- 然后 CPU0 在跑进程 A，而 CPU1 在跑进程 B，A 和 B 同时对这个锁发起了 `acquire`；
- 我们所希望看到的是，锁被且只被 A 或 B 中的一个进程锁住，而另一个进程则继续在 for 中循环等待。
- 但是，假设 CPU0 和 CPU1 同时执行到了第6行，那么会发生什么呢？
- 没错，两个 CPU 都发现 `lk->locked == 0`，因为另一个 CPU 还没有来得及改动 `lk->locked`;
- 这就会导致 A 和 B 同时获得锁，并从 `acquire` 中返回，这显然不是我们希望看到的。

那 acquire 到底是怎么实现的呢？这个仅凭软件是无法实现的，需要硬件参与辅助实现。 

在这里我们先介绍一条特殊的 CPU 指令 `amoswap`。`amoswap` 实现了在一条指令完成一次 load 和 store，更具体的说，就是可以**将一个寄存器的值和某一内存地址的值做交换，将指定内存地址的值放入寄存器，同时将寄存器的值放入内存。此外，CPU 还会保证某一 CPU 核在执行这一指令时，其他 CPU 核不能读或写对应的内存地址。**

在 C 语言中使用这一汇编指令的语句调用是 `__sync_lock_test_and_set()`。 这一函数**接收两个参数，要写入的地址和要写入的值，返回值是写入地址原来的值**。 请仔细比较 `amoswap` 指令和 `__sync_lock_test_and_set()` 行为上的相似之处， 确保你已经大致知道 `__sync_lock_test_and_set()` 是如何使用 `amoswap` 实现功能。

了解了 `__sync_lock_test_and_set()` 之后，我们在回到 acquire 的问题上来。先看看 xv6 中是如何实现 acquire 的。

```c
// Acquire the lock.
// Loops (spins) until the lock is acquired.
void acquire(struct spinlock *lk)
{
  push_off(); // disable interrupts to avoid deadlock.
  if (holding(lk))
    panic("acquire");
  __sync_fetch_and_add(&(lk->n), 1);
  // On RISC-V, sync_lock_test_and_set turns into anatomic swap:
  //   a5 = 1
  //   s1 = &lk->locked
  //   amoswap.w.aq a5, a5, (s1)
  while (__sync_lock_test_and_set(&lk->locked, 1) != 0)
  {
    __sync_fetch_and_add(&lk->nts, 1);
  }

  // Tell the C compiler and the processor to not moveloads or stores
  // past this point, to ensure that the criticalsection's memory
  // references happen after the lock is acquired.
  __sync_synchronize();
  // Record info about lock acquisition for holding() anddebugging.
  lk->cpu = mycpu();
}
```

我们需要注意的是 while 循环。这里使用了 `__sync_lock_test_and_set()`（使用 `amoswap`），保证了对 `lk->locked` 读写的一致性。下面我们来分析以下具体的工作流程。

`__sync_lock_test_and_set(&lk->locked, 1)`

- 要写入的地址：`&lk->locked`；要写入的值 1。
- 返回值：**经过函数操作过的**，`lk->locked` 的值。
- 函数进行的操作：
  - 给 a5 寄存器赋值——【要写入的值 1】：`a5 = 1`。
  - 给 s1 寄存器赋值——【要写入的地址 `&lk->locked`】：`s1 = &lk->locked`。
  - `amoswap`：**原子操作**
    - 从 s1 load 来 `lk->locked` 的值。
    - 把该值放入 a5 寄存器。
    - store：把 a5 寄存器的值放入 `lk->locked` （地址）

- 当 `lk->locked == 0`，即当前锁空闲时，`__sync_lock_test_and_set()` 在返回 0 的同时，会往 `lk->locked` 写入 1，即上锁。整个过程因为由 `amoswap` 实现，所以不会被其的 CPU 核所干扰。返回 0 之后退出 while 循环，符合我们的预期。
- 当 `lk->locked == 1`，即当前锁被锁定时，`__sync_lock_test_and_set()` 在返回 1 的同时，往 `lk->locked` 里写入了 1。事实上在锁被锁定后我们不应该往 `lk->locked` 里写东西（当然，除了解锁的时候），但是因为锁本来就是 1，再写入一个 1 相当于没有改变内容，所以也没差。同时，因为返回了 1，我们知道当前锁被锁住了，所以会继续在 while 中循环。

这真是十分精巧的交换。实际上，交换的原子性保证了：**获取锁的状态和 `acquire` 锁是一个完整的事件**。不仅获得，还连贯的改变了它的值。

我们再用 `release` 举例：

```c
// Release the lock.
void release(struct spinlock *lk)
{
  if (!holding(lk))
    panic("release");

  lk->cpu = 0;

  // Tell the C compiler and the CPU to not move loads or stores
  // past this point, to ensure that all the stores in the critical
  // section are visible to other CPUs before the lock is released,
  // and that loads in the critical section occur strictly before
  // the lock is released.
  // On RISC-V, this emits a fence instruction.
  __sync_synchronize();

  // Release the lock, equivalent to lk->locked = 0.
  // This code doesn't use a C assignment, since the C standard
  // implies that an assignment might be implemented with
  // multiple store instructions.
  // On RISC-V, sync_lock_release turns into an atomic swap:
  //   s1 = &lk->locked
  //   amoswap.w zero, zero, (s1)
  __sync_lock_release(&lk->locked);

  pop_off();
}
```

`__sync_lock_release(&lk->locked)`

- 要写入的地址：`&lk->locked`；要写入的值【默认为 0】。
- 返回值：**经过函数操作过的**，`lk->locked` 的值。
- 函数进行的操作：
  - ~~给 a5 寄存器赋值——【要写入的值 1】：`a5 = 1`。~~ （zero 寄存器值一直为 0）
  - 给 s1 寄存器赋值——【要写入的地址 `&lk->locked`】：`s1 = &lk->locked`。
  - `amoswap`：**原子操作**
    - 从 s1 load 来 `lk->locked` 的值。
    - ~~把该值放入 a5 寄存器。~~ （不需要）
    - store：把 a0 寄存器的值 0 放入 `lk->locked` （地址）
- 在执行函数前，一定有 `lk->locked == 1`；在执行函数后，一定有 `lk->locked == 0`。
- `__sync_lock_release(&lk->locked)` 在返回 0 的同时，会往 `lk->locked` 写入 0，即解锁。整个过程因为由 `amoswap` 实现，所以不会被其的 CPU 核所干扰。返回 0 之后退出 while 循环，符合我们的预期。

## 2 try_acquire

我们模仿 `acquire`，写一个尝试获取锁的 `try_acquire`。

相比于 `acquire`，它的特点是：尝试获取锁，如果没锁，那么就获取锁，**仅进行一次这样的判断**。

我们在 `kernel/spinlock.c` 中添加函数：

```c
int try_acquire(struct spinlock *lk)
{
  push_off();
  if (holding(lk))
    panic("acquire");

  __sync_fetch_and_add(&(lk->n), 1);

  if (__sync_lock_test_and_set(&lk->locked, 1) == 0)
  {
    __sync_fetch_and_add(&(lk->nts), 1);
    __sync_synchronize();
    lk->cpu = mycpu();
    return 1;
  }
  else
  {
    // if fail, pop_off()
    pop_off();
    return 0;
  }
}
```

注意：

1. 获取失败时记得打开中断 `pop_off()`。
3. 记得修改 `kernel/def.h`。

## 3 最终实现

只展示 bget：

```c
static struct buf *
bget(uint dev, uint blockno)
{
  struct buf *b;
  struct bucket *buk_key;
  struct bucket *buk;

  buk_key = &bcache.bucket[hash(blockno)];

  acquire(&buk_key->lock);

  for (b = buk_key->head.next; b != &buk_key->head; b = b->next)
  {
    if (b->dev == dev && b->blockno == blockno)
    {
      b->refcnt++;
      release(&buk_key->lock);
      acquiresleep(&b->lock);
      return b;
    }
  }

  // Not cached.
  // find LRU in the hash(blockno) bucket
  // remind that still own buk_key->lock
  for (b = buk_key->head.prev; b != &buk_key->head; b = b->prev)
  {
    if (b->refcnt == 0)
    {
      b->dev = dev;
      b->blockno = blockno;
      b->valid = 0;
      b->refcnt = 1;
      release(&buk_key->lock);
      acquiresleep(&b->lock);
      return b;
    }
  }

LOOP:
  // we cannot find unused buf in the hash(blockno) bucket
  // search in other bucket
  for (buk = bcache.bucket; buk < bcache.bucket + NBUCKETS; buk++)
  {
    // do not search hash(blockno) bucket again
    if (buk == buk_key)
      continue;

    // try acquire
    if (try_acquire(&buk->lock))
    {
      for (b = buk->head.prev; b != &buk->head; b = b->prev)
      {
        if (b->refcnt == 0)
        {
          b->dev = dev;
          b->blockno = blockno;
          b->valid = 0;
          b->refcnt = 1;

          remove(b);
          add_head(&buk_key->head, b);

          release(&buk->lock);
          release(&buk_key->lock);
          acquiresleep(&b->lock);
          return b;
        }
      }
      release(&buk->lock);
    }
  }

  goto LOOP;

  release(&buk_key->lock);

  panic("bget: no buffers");
}
```

## 4 测试结果

两个测试都可以过。看 tot 的值，效果不错。

建议按照先测试 `bcachetest`，再测试 `usertests` 的顺序。因为 `bcachetest` 会记录 `usertests` 的竞争的情况，导致 fetch-and-add 数较大，tot 值也很大，但是由于 test0 只记录 test0 的数据，所以又可以过 test0，会显示 OK。

```c
$ bcachetest
start test0
test0 results:
--- lock kmem/bcache stats
lock: kmems0: #fetch-and-add 0 #acquire() 32952
lock: kmems1: #fetch-and-add 0 #acquire() 40
lock: kmems3: #fetch-and-add 0 #acquire() 7
lock: kmems4: #fetch-and-add 0 #acquire() 41
lock: kmems5: #fetch-and-add 0 #acquire() 10
lock: kmems7: #fetch-and-add 0 #acquire() 59
lock: bcache.bucket: #fetch-and-add 2 #acquire() 4120
lock: bcache.bucket: #fetch-and-add 0 #acquire() 4120
lock: bcache.bucket: #fetch-and-add 0 #acquire() 2270
lock: bcache.bucket: #fetch-and-add 0 #acquire() 4276
lock: bcache.bucket: #fetch-and-add 0 #acquire() 2264
lock: bcache.bucket: #fetch-and-add 0 #acquire() 4264
lock: bcache.bucket: #fetch-and-add 0 #acquire() 4738
lock: bcache.bucket: #fetch-and-add 21 #acquire() 6680
lock: bcache.bucket: #fetch-and-add 0 #acquire() 8650
lock: bcache.bucket: #fetch-and-add 0 #acquire() 6174
lock: bcache.bucket: #fetch-and-add 0 #acquire() 6174
lock: bcache.bucket: #fetch-and-add 0 #acquire() 6180
lock: bcache.bucket: #fetch-and-add 0 #acquire() 6176
--- top 5 contended locks:
lock: virtio_disk: #fetch-and-add 646071 #acquire() 1206
lock: proc: #fetch-and-add 362197 #acquire() 220948
lock: proc: #fetch-and-add 314558 #acquire() 220586
lock: proc: #fetch-and-add 313022 #acquire() 220588
lock: proc: #fetch-and-add 183939 #acquire() 220589
tot= 23
test0: OK
start test1
test1 OK
```

我们可以加入输出语句检查一下到底有没有出现 `try_acquire` 应用的情况，即 “查看锁，发现上锁了就不去 `acquire`” 这种事情是否发生过。

我们把 CPU 数量调整为 8，这样在多核的情况下，竞争会更加明显，也就能更容易的显示出是否发生了竞争。

我们测试 `bcachetest`，发现没有输出，即没有这种情况；我们测试 `usertest`，发现有多条输出夹杂在其他输出中，那么说明有这种情况。这说明我们的优化是有效的。