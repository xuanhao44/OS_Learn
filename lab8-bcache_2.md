# MIT 6.S081 - Lab Lock Buffer cache (2) timestamp 单项优化

## 1 ticks 使用

时间戳可通过 `kernel/trap.c` 中的 ticks 函数获得（ticks 已在 `kernel/def.h` 中声明，bio.c 中可直接使用）。

`kernel/def.h`（部分截取）

```c
// trap.c
extern uint     ticks;
void            trapinit(void);
void            trapinithart(void);
extern struct spinlock tickslock;
void            usertrapret(void);
```

使用 ticks 需要取得 tickslock 自旋锁。

使用例子：`kernel/sysproc.c`（部分截取）

```c
// return how many clock tick interrupts have occurred
// since start.
uint64
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}
```

## 2 timestamp 的意义

timestamp 记录的是什么时间？——是缓存块最后一次被访问的时间。那么依照原来的设计，buf 的 timestamp 被更新的时间应该在 brelse 中（当 refcnt = 0 时）。

当然，也有人在 bget 的时候更新 timestamp。这也是可以的。上层文件系统在访问一个缓存块，这个完整的过程是必有 bget 和 brelse 的。从这一点上看，他们都能表示 "最后一次被访问"。

这里的实现选择在 brelse 中更新。

## 3 来自指导书的 hint 理解

来自指导书的 hint1：**移除空闲缓存块列表(`bcache.head`)**

原始的设计维护了一个 LRU 的链表，对这种有序的维护主要是由 brelse 完成的：每当 refcnt = 0 的时候，brelse 就会把 buf 移动到 most recent use 端，这样便一直维持了 LRU 的链表。LRU 链表就是以缓存块最后一次被访问的时间为标志的有序的结构。

移除 head 意味着什么？意味着用链表来高效维护的，以 brelse 驱动的设计不再被使用了。我们该如何利用 timestamp 来构建新的设计呢？

*这个问题有点大，不太好三两句话说完。下面的设计将会一步步说明。*

让我们先移除这个 head 吧，同时我们也不需要 prev 和 next 了，加上 timestamp。

显然所有函数都需要大改了，我们的改造才刚刚开始。

来自导书的 hint2：**此项改动可使`brelse`不再需要锁上`bcache lock`。**

原本的设计，brelse 不仅改变了链表的结构，也改变了某个元素的值（b->refcnt - 1），于是需要给这些操作加上 `bcache lock`。

如果不加上 `bcache lock`，又没有其他措施的话，是无法满足 brelse 的要求的——据我们上面的分析，brelse 中需要 b->refcnt - 1，还需要 b->timestamp = ticks。怎么办呢？

答案是：**为 buf 添加一个新的自旋锁。**在对 buf 进行操作的时候，便加上这个自旋锁，从而不使用 `bcache lock`。

所以我们现在对 buf 的定义修正为：

`kernel/buf.h`

```c
struct buf {
  struct spinlock spinlock;
  struct sleeplock sleeplock;
  int valid;   // has data been read from disk?
  int disk;    // does disk "own" buf?
  uint dev;
  uint blockno;
  uint refcnt;
  uint timestamp;
  uchar data[BSIZE];
};
```

*为了避免把自旋锁和睡眠锁搞混，改变了其名称。*

## 4 改进 part1：binit、bread、brelse、bpin、bunpin

`binit` 很好说，把所有的锁初始化。因为不需要维护链表了，所以少了很多填充链表的代码。注意要以 bache 开头，不然测试时会出问题。

```c
void binit(void)
{
  struct buf *b;

  initlock(&bcache.lock, "bcache");
  for (b = bcache.buf; b < bcache.buf + NBUF; b++)
  {
    // 不能在自旋锁保护的临界区中使用睡眠锁，但是可以在睡眠锁保护的临界区中使用自旋锁。
    // 总之就是 sleeplock 要在外面，或者没关系
    initsleeplock(&b->sleeplock, "bcache.buffer.sleeplock");
    initlock(&b->spinlock, "bcache.buffer.spinlock");
  }
}
```

`brelse` 不再需要锁上`bcache lock`，检测到 refcnt = 0 的时候更新 timestamp。至于 `releasesleep(&b->sleeplock);` 是在 spinlock 之前还是之后，应该是都可以的。

```c
// Release a locked buffer.
void brelse(struct buf *b)
{
  if (!holdingsleep(&b->sleeplock))
    panic("brelse");
  
  releasesleep(&b->sleeplock);

  acquire(&b->spinlock);
  b->refcnt--;
  if (b->refcnt == 0)
  {
    acquire(&tickslock);
    b->timestamp = ticks;
    release(&tickslock);
  }
  release(&b->spinlock);
}
```

对于 `bpin` 和 `bunpin`，它们的锁可以改为 buf 的 spinlock。

```c
void bpin(struct buf *b)
{
  acquire(&b->spinlock);
  b->refcnt++;
  release(&b->spinlock);
}

void bunpin(struct buf *b)
{
  acquire(&b->spinlock);
  b->refcnt--;
  release(&b->spinlock);
}
```

`bwrite` 不用修改逻辑；`bread` 的逻辑修改有待考虑（也许可以在修改 buf 的时候加上 spinlock 锁）。

```c
// Return a locked buf with the contents of the indicated block.
struct buf *
bread(uint dev, uint blockno)
{
  struct buf *b;

  b = bget(dev, blockno);
  // if not cached, valid will be set to 0
  if (!b->valid)
  {
    virtio_disk_rw(b, 0);
    b->valid = 1;
  }
  return b;
}

// Write b's contents to disk.  Must be locked.
void bwrite(struct buf *b)
{
  if (!holdingsleep(&b->sleeplock))
    panic("bwrite");
  virtio_disk_rw(b, 1);
}
```

## 5 改进 part2：重头戏 bget

bget 显然是最重要的，它是磁盘缓存的核心。

如果简单的想，bget 需要被 `bcache.lock` 所保护。

在其中对每个 buf 的操作中，考虑到安全性，我们理所应当的加上其 spinlock 再进行处理。

### 5.1 严格检查

```c
static struct buf *
bget(uint dev, uint blockno)
{
  struct buf *b;

  acquire(&bcache.lock);

  // Is the block already cached?
  for (b = bcache.buf; b < bcache.buf + NBUF; b++)
  {
    acquire(&b->spinlock);
    if (b->dev == dev && b->blockno == blockno)
    {
      b->refcnt++; // 该块被引用次数 ++
      release(&b->spinlock);
      release(&bcache.lock);
      acquiresleep(&b->sleeplock);
      return b;
    }
    else
      release(&b->spinlock);
  }

  // Not cached.
  // **Recycle** the least recently used (LRU) unused buffer.
  uint time_least = 0xffffffff;
  struct buf *lrub = (void*) 0;
  for (b = bcache.buf; b < bcache.buf + NBUF; b++)
  {
    acquire(&b->spinlock);
    // refcnt == 0 means unused
    if (b->refcnt == 0 && b->timestamp < time_least)
    {
      time_least = b->timestamp;
      lrub = b;
    }
    release(&b->spinlock);
  }
  
  acquire(&lrub->spinlock);
  if (lrub != (void*) 0)
  {
    lrub->dev = dev;
    lrub->blockno = blockno;
    lrub->valid = 0; // set valid 0, wait for virtio_disk_rw()
    lrub->refcnt = 1;
    release(&lrub->spinlock);
    release(&bcache.lock);
    acquiresleep(&lrub->sleeplock);
    return lrub;
  }
  else
  {
    release(&lrub->spinlock);
    release(&bcache.lock);
  }

  panic("bget: no buffers");
}
```

可以过 usertests，也就是说安全性可以保证；

但是不能过 bcachetest，下面是输出结果：

```shell
$ bcachetest
start test0
test0 results:
--- lock kmem/bcache stats
lock: kmems0: #fetch-and-add 0 #acquire() 32980
lock: kmems1: #fetch-and-add 0 #acquire() 76
lock: kmems2: #fetch-and-add 0 #acquire() 25
lock: bcache: #fetch-and-add 1596487 #acquire() 32924
--- top 5 contended locks:
lock: bcache: #fetch-and-add 1596487 #acquire() 32924
lock: virtio_disk: #fetch-and-add 90355 #acquire() 1182
lock: proc: #fetch-and-add 56558 #acquire() 77713
lock: proc: #fetch-and-add 17000 #acquire() 77361
lock: proc: #fetch-and-add 14397 #acquire() 77361
tot= 1596487
test0: FAIL
start test1
test1 OK
```

可以看到，锁争用的情况是相当严重的。远比原本的设计严重。

### 5.2 double check

为了改善上面严格的设计，我们进行进一步优化。

**采用 “不安全 check + 加锁 double check” 的处理模式：利用某些可以允许的不安全来换取缩小的锁范围，然后再使用额外的手段保证正确性。**

```c
static struct buf *
bget(uint dev, uint blockno)
{
  struct buf *b;

  acquire(&bcache.lock);

  // Is the block already cached?
  for (b = bcache.buf; b < bcache.buf + NBUF; b++)
  {
    // unsafe check
    if (b->dev == dev && b->blockno == blockno)
    {
      // acquire lock
      acquire(&b->spinlock);
      // double check
      if (b->dev == dev && b->blockno == blockno)
      {
        b->refcnt++; // 该块被引用次数 ++
        release(&b->spinlock);
        release(&bcache.lock);
        acquiresleep(&b->sleeplock);
        return b;
      }
      else
        release(&b->spinlock);
    }
  }

  // Not cached.
  // **Recycle** the least recently used (LRU) unused buffer.
  uint time_least = 0xffffffff;
  struct buf *lrub = (void *)0;
  for (b = bcache.buf; b < bcache.buf + NBUF; b++)
  {

    // refcnt == 0 means unused
    if (b->refcnt == 0 && b->timestamp < time_least)
    {
      acquire(&b->spinlock);
      if (b->refcnt == 0 && b->timestamp < time_least)
      {
        time_least = b->timestamp;
        lrub = b;
      }
      release(&b->spinlock);
    }
  }

  if (lrub != (void *)0)
  {
    acquire(&lrub->spinlock);
    if (lrub != (void *)0)
    {
      lrub->dev = dev;
      lrub->blockno = blockno;
      lrub->valid = 0; // set valid 0, wait for virtio_disk_rw()
      lrub->refcnt = 1;
      release(&lrub->spinlock);
      release(&bcache.lock);
      acquiresleep(&lrub->sleeplock);
      return lrub;
    }
    else
    {
      release(&lrub->spinlock);
      release(&bcache.lock);
    }
  }

  panic("bget: no buffers");
}
```

可以过 usertests，也就是说安全性可以保证；

但是不能过 bcachetest，下面是输出结果：

```shell
$ bcachetest
start test0
test0 results:
--- lock kmem/bcache stats
lock: kmems0: #fetch-and-add 0 #acquire() 32981
lock: kmems1: #fetch-and-add 0 #acquire() 50
lock: kmems2: #fetch-and-add 0 #acquire() 58
lock: bcache: #fetch-and-add 87859 #acquire() 32924
--- top 5 contended locks:
lock: proc: #fetch-and-add 113056 #acquire() 75793
lock: bcache: #fetch-and-add 87859 #acquire() 32924
lock: virtio_disk: #fetch-and-add 68156 #acquire() 1182
lock: proc: #fetch-and-add 21129 #acquire() 75460
lock: proc: #fetch-and-add 19293 #acquire() 75439
tot= 87859
test0: FAIL
start test1
test1 OK
```

可以看到，锁争用的情况比严格的情况好多了，但是既不能通过测试，也还是不如原本的设计。

### 5.3 查找命中不用大锁保护（有错误）

有人提出，bget 在查找的时候不需要 `bcache.lock`，只需要在未命中的时候开始加上 `bcache.lock`。

```c
static struct buf *
bget(uint dev, uint blockno)
{
  struct buf *b;

  // Is the block already cached?
  for (b = bcache.buf; b < bcache.buf + NBUF; b++)
  {
    acquire(&b->spinlock);
    if (b->dev == dev && b->blockno == blockno)
    {
      b->refcnt++; // 该块被引用次数 ++
      release(&b->spinlock);
      acquiresleep(&b->sleeplock);
      return b;
    }
    else
      release(&b->spinlock);
  }

  acquire(&bcache.lock);

  // Not cached.
  // **Recycle** the least recently used (LRU) unused buffer.
  uint time_least = 0xffffffff;
  struct buf *lrub = (void*) 0;
  for (b = bcache.buf; b < bcache.buf + NBUF; b++)
  {
    acquire(&b->spinlock);
    // refcnt == 0 means unused
    if (b->refcnt == 0 && b->timestamp < time_least)
    {
      time_least = b->timestamp;
      lrub = b;
    }
    release(&b->spinlock);
  }
  
  acquire(&lrub->spinlock);
  if (lrub != (void*) 0)
  {
    lrub->dev = dev;
    lrub->blockno = blockno;
    lrub->valid = 0; // set valid 0, wait for virtio_disk_rw()
    lrub->refcnt = 1;
    release(&lrub->spinlock);
    release(&bcache.lock);
    acquiresleep(&lrub->sleeplock);
    return lrub;
  }
  else
  {
    release(&lrub->spinlock);
    release(&bcache.lock);
  }

  panic("bget: no buffers");
}
```

可以过 bcachetest，tot = 0。

```shell
$ bcachetest
start test0
test0 results:
--- lock kmem/bcache stats
lock: kmems0: #fetch-and-add 0 #acquire() 32955
lock: kmems1: #fetch-and-add 0 #acquire() 118
lock: kmems2: #fetch-and-add 0 #acquire() 22
lock: bcache: #fetch-and-add 0 #acquire() 85
--- top 5 contended locks:
lock: proc: #fetch-and-add 129503 #acquire() 77183
lock: virtio_disk: #fetch-and-add 79408 #acquire() 1194
lock: proc: #fetch-and-add 22881 #acquire() 76830
lock: time: #fetch-and-add 17670 #acquire() 32650
lock: proc: #fetch-and-add 14995 #acquire() 76830
tot= 0
test0: OK
start test1
test1 OK
```

这真令人开心！

但是，usertests 却失败了，也就是说，正确性没有了——这是简直是晴天霹雳！

先测试 bcachetest，再测试 usertests 会有如下结果。

```shell
$ usertests
usertests starting
test manywrites: panic: create: dirlink
```

先测试 usertests 会有如下结果。

```shell
$ usertests
usertests starting
test manywrites: panic: ilock: no type
```

这种改进到底出了什么错呢？

## 5.4 安全性处理（1）

5.3 bget 逻辑为：先查找数组是否存在缓存块，存在就返回，不存在就获取大锁，从数组中拿一个 refcnt = 0 的缓存块来使用。

问题是你**获取大锁后没有再次检测是不是缓存块已经存在了**，也就是说，之前检测命中的时候没有缓存块，但是再次检测命中的时候有这个缓存块了，你如果不去再次检测，那就不知道这种情况，就会拿一个 LRU 块去指向这个磁盘块——这样数组里面就有两个缓存块指向同一个磁盘块了。

也就是说需要在获取大锁后再次检测。那这样和原本用大锁包住整个 bget 有什么区别呢？为啥要做检测，加锁，再次检测的事情呢？——答案也许很明显了，这是一个很大的 double check！

*double check 爽，一直 double check 一直爽.jpg*

再次检查的部分好说，那我们该怎么写 ”第一次检测" 呢？

到底时要加大锁和小锁，还是只加小锁呢？

```c
  // 第一次检测, 99% 会命中在第一次检测
  // Is the block already cached?
  for (b = bcache.buf; b < bcache.buf + NBUF; b++)
  {
    // unsafe check
    if (b->dev == dev && b->blockno == blockno)
    {
      // acquire lock
      acquire(&bcache.lock);
      acquire(&b->spinlock);
      // double check
      if (b->dev == dev && b->blockno == blockno)
      {
        b->refcnt++; // 该块被引用次数 ++
        release(&b->spinlock);
        release(&bcache.lock);
        acquiresleep(&b->sleeplock);
        return b;
      }
      else {
        release(&b->spinlock);
        release(&bcache.lock);
      }
    }
  }
```

测试结果：

- 如果都加，那么就能保证安全性（usertests 能过），但是 test1 过不了，tot 过大；

- 如果只加小锁，那么 tot 等于 0，但是不安全按（usertests 报 panic）。

这便使人犯难了。不加大锁不安全，加了大锁争用多，如之奈何？

## 5.4 安全性处理（2）

来看看只加小锁的问题吧。

未命中寻找 LRU 块时，找到了一个这样的块，正准备更新，但是这个块被另一个进程在第一次无大锁检测阶段拿走了。（注意为何能被拿走，可以想一想）

当第一次检测也加上大锁的时候，就不会发生这样的事，因为大锁保护了数组，使其在一个进程访问时不会被另一个进程访问。

同时注意到，能被拿走的原因正是因为一个很小的逻辑漏洞——**遍历寻找 timestamp 最小的缓存块，但是当这个块的时间戳最小时应该会一直拿着它的锁**。而我之前的实现里，为了方便，其实一直忽视了这个细节，因为不太好处理。

了解概念：乐观锁和悲观锁。

- 防止被抢走就是悲观锁，被抢走后重试就是乐观锁。
- 冲突非常频繁悲观锁快，冲突很少就乐观锁快。
- 乐观锁的实现相对简单。

我们可以选择悲观锁，也就是一直锁着这个时间戳最小的块；也可以选择乐观锁，允许你被抢走，我到时候再来检查一次，如果确实被抢走了，我就再查找一次。

这里我们使用乐观锁。怎么才是被抢走了呢？那自然是用 refcnt == 0 来判断。如果真的被抢走了，那么 refcnt 的值就不会是 0 了。如何重试呢？我们直接使用 goto 返回到再次检测的位置。

```c
static struct buf *
bget(uint dev, uint blockno)
{
  struct buf *b;

  // 第一次检测, 99% 会命中在第一次检测
  // 但是没有大锁保护, 它可能会抢走别人的
  for (b = bcache.buf; b < bcache.buf + NBUF; b++)
  {
    // unsafe check
    if (b->dev == dev && b->blockno == blockno)
    {
      // acquire lock
      acquire(&b->spinlock);
      // double check
      if (b->dev == dev && b->blockno == blockno)
      {
        b->refcnt++; // 该块被引用次数 ++
        release(&b->spinlock);
        acquiresleep(&b->sleeplock);
        return b;
      }
      else
        release(&b->spinlock);
    }
  }

  acquire(&bcache.lock);
// 再次检测
LOOP:
  for (b = bcache.buf; b < bcache.buf + NBUF; b++)
  {
    // unsafe check
    if (b->dev == dev && b->blockno == blockno)
    {
      // acquire lock
      acquire(&b->spinlock);
      // double check
      if (b->dev == dev && b->blockno == blockno)
      {
        b->refcnt++; // 该块被引用次数 ++
        release(&b->spinlock);
        release(&bcache.lock);
        acquiresleep(&b->sleeplock);
        return b;
      }
      else
        release(&b->spinlock);
    }
  }

  // Not cached.
  // **Recycle** the least recently used (LRU) unused buffer.
  uint time_least = 0xffffffff;
  struct buf *lrub = (void *)0;
  for (b = bcache.buf; b < bcache.buf + NBUF; b++)
  {
    // refcnt == 0 means unused
    if (b->refcnt == 0 && b->timestamp < time_least)
    {
      acquire(&b->spinlock);
      if (b->refcnt == 0 && b->timestamp < time_least)
      {
        time_least = b->timestamp;
        lrub = b;
      }
      release(&b->spinlock);
    }
  }

  if (lrub != (void *)0 && lrub->refcnt == 0)
  {
    acquire(&lrub->spinlock);
    if (lrub != (void *)0 && lrub->refcnt == 0)
    {
      lrub->dev = dev;
      lrub->blockno = blockno;
      lrub->valid = 0; // set valid 0, wait for virtio_disk_rw()
      lrub->refcnt = 1;
      release(&lrub->spinlock);
      release(&bcache.lock);
      acquiresleep(&lrub->sleeplock);
      return lrub;
    }
    else
    {
      release(&lrub->spinlock);
      // release(&bcache.lock); no need
      goto LOOP; // goto second check
    }
  }
  else
  {
    // release(&bcache.lock); no need
    goto LOOP; // goto second check
  }

  panic("bget: no buffers");
}
```

测试结果：tot = 0；usertests 也过了，完美！

```shell
$ bcachetest
start test0
test0 results:
--- lock kmem/bcache stats
lock: kmems0: #fetch-and-add 0 #acquire() 33010
lock: kmems1: #fetch-and-add 0 #acquire() 1
lock: kmems2: #fetch-and-add 0 #acquire() 57
lock: kmems3: #fetch-and-add 0 #acquire() 18
lock: kmems7: #fetch-and-add 0 #acquire() 20
lock: bcache: #fetch-and-add 0 #acquire() 85
lock: bcache.buffer.spinlock: #fetch-and-add 0 #acquire() 2164
lock: bcache.buffer.spinlock: #fetch-and-add 0 #acquire() 2206
lock: bcache.buffer.spinlock: #fetch-and-add 0 #acquire() 2342
lock: bcache.buffer.spinlock: #fetch-and-add 0 #acquire() 4378
lock: bcache.buffer.spinlock: #fetch-and-add 0 #acquire() 2089
lock: bcache.buffer.spinlock: #fetch-and-add 0 #acquire() 2114
lock: bcache.buffer.spinlock: #fetch-and-add 0 #acquire() 2158
lock: bcache.buffer.spinlock: #fetch-and-add 0 #acquire() 2540
lock: bcache.buffer.spinlock: #fetch-and-add 0 #acquire() 2202
lock: bcache.buffer.spinlock: #fetch-and-add 0 #acquire() 2202
lock: bcache.buffer.spinlock: #fetch-and-add 0 #acquire() 2194
lock: bcache.buffer.spinlock: #fetch-and-add 0 #acquire() 2138
lock: bcache.buffer.spinlock: #fetch-and-add 0 #acquire() 2108
lock: bcache.buffer.spinlock: #fetch-and-add 0 #acquire() 2070
lock: bcache.buffer.spinlock: #fetch-and-add 0 #acquire() 2066
lock: bcache.buffer.spinlock: #fetch-and-add 0 #acquire() 2072
lock: bcache.buffer.spinlock: #fetch-and-add 0 #acquire() 2063
lock: bcache.buffer.spinlock: #fetch-and-add 0 #acquire() 2067
lock: bcache.buffer.spinlock: #fetch-and-add 0 #acquire() 2063
lock: bcache.buffer.spinlock: #fetch-and-add 0 #acquire() 2063
lock: bcache.buffer.spinlock: #fetch-and-add 0 #acquire() 2063
lock: bcache.buffer.spinlock: #fetch-and-add 0 #acquire() 2314
lock: bcache.buffer.spinlock: #fetch-and-add 0 #acquire() 2104
lock: bcache.buffer.spinlock: #fetch-and-add 0 #acquire() 2062
lock: bcache.buffer.spinlock: #fetch-and-add 0 #acquire() 2062
lock: bcache.buffer.spinlock: #fetch-and-add 0 #acquire() 2070
lock: bcache.buffer.spinlock: #fetch-and-add 0 #acquire() 2070
lock: bcache.buffer.spinlock: #fetch-and-add 0 #acquire() 2070
lock: bcache.buffer.spinlock: #fetch-and-add 0 #acquire() 2070
lock: bcache.buffer.spinlock: #fetch-and-add 0 #acquire() 2070
--- top 5 contended locks:
lock: proc: #fetch-and-add 654904 #acquire() 221450
lock: proc: #fetch-and-add 554628 #acquire() 221806
lock: virtio_disk: #fetch-and-add 468252 #acquire() 1194
lock: proc: #fetch-and-add 360263 #acquire() 221449
lock: proc: #fetch-and-add 351016 #acquire() 221448
tot= 0
test0: OK
start test1
test1 OK
```

## 最终代码

```c
// Buffer cache.
//
// The buffer cache is a linked list of buf structures holding
// cached copies of disk block contents.  Caching disk blocks
// in memory reduces the number of disk reads and also provides
// a synchronization point for disk blocks used by multiple processes.
//
// Interface:
// * To get a buffer for a particular disk block, call bread.
// * After changing buffer data, call bwrite to write it to disk.
// * When done with the buffer, call brelse.
// * Do not use the buffer after calling brelse.
// * Only one process at a time can use a buffer,
//     so do not keep them longer than necessary.

#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "buf.h"

struct
{
  struct spinlock lock;
  struct buf buf[NBUF];
} bcache;

void binit(void)
{
  struct buf *b;

  initlock(&bcache.lock, "bcache");
  for (b = bcache.buf; b < bcache.buf + NBUF; b++)
  {
    // 不能在自旋锁保护的临界区中使用睡眠锁，但是可以在睡眠锁保护的临界区中使用自旋锁。
    // 总之就是 sleeplock 要在外面，或者没关系
    initsleeplock(&b->sleeplock, "bcache.buffer.sleeplock");
    initlock(&b->spinlock, "bcache.buffer.spinlock");
  }
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf *
bget(uint dev, uint blockno)
{
  struct buf *b;

  // 第一次检测, 99% 会命中在第一次检测
  // 但是没有大锁保护, 它可能会抢走别人的
  for (b = bcache.buf; b < bcache.buf + NBUF; b++)
  {
    // unsafe check
    if (b->dev == dev && b->blockno == blockno)
    {
      // acquire lock
      acquire(&b->spinlock);
      // double check
      if (b->dev == dev && b->blockno == blockno)
      {
        b->refcnt++; // 该块被引用次数 ++
        release(&b->spinlock);
        acquiresleep(&b->sleeplock);
        return b;
      }
      else
        release(&b->spinlock);
    }
  }

  acquire(&bcache.lock);
// 再次检测
LOOP:
  for (b = bcache.buf; b < bcache.buf + NBUF; b++)
  {
    // unsafe check
    if (b->dev == dev && b->blockno == blockno)
    {
      // acquire lock
      acquire(&b->spinlock);
      // double check
      if (b->dev == dev && b->blockno == blockno)
      {
        b->refcnt++; // 该块被引用次数 ++
        release(&b->spinlock);
        release(&bcache.lock);
        acquiresleep(&b->sleeplock);
        return b;
      }
      else
        release(&b->spinlock);
    }
  }

  // Not cached.
  // **Recycle** the least recently used (LRU) unused buffer.
  uint time_least = 0xffffffff;
  struct buf *lrub = (void *)0;
  for (b = bcache.buf; b < bcache.buf + NBUF; b++)
  {
    // refcnt == 0 means unused
    if (b->refcnt == 0 && b->timestamp < time_least)
    {
      acquire(&b->spinlock);
      if (b->refcnt == 0 && b->timestamp < time_least)
      {
        time_least = b->timestamp;
        lrub = b;
      }
      release(&b->spinlock);
    }
  }

  if (lrub != (void *)0 && lrub->refcnt == 0)
  {
    acquire(&lrub->spinlock);
    if (lrub != (void *)0 && lrub->refcnt == 0)
    {
      lrub->dev = dev;
      lrub->blockno = blockno;
      lrub->valid = 0; // set valid 0, wait for virtio_disk_rw()
      lrub->refcnt = 1;
      release(&lrub->spinlock);
      release(&bcache.lock);
      acquiresleep(&lrub->sleeplock);
      return lrub;
    }
    else
    {
      release(&lrub->spinlock);
      // release(&bcache.lock); no need
      goto LOOP; // goto second check
    }
  }
  else
  {
    // release(&bcache.lock); no need
    goto LOOP; // goto second check
  }

  panic("bget: no buffers");
}

// Return a locked buf with the contents of the indicated block.
struct buf *
bread(uint dev, uint blockno)
{
  struct buf *b;

  b = bget(dev, blockno);
  // if not cached, valid will be set to 0
  if (!b->valid)
  {
    virtio_disk_rw(b, 0);
    b->valid = 1;
  }
  return b;
}

// Write b's contents to disk.  Must be locked.
void bwrite(struct buf *b)
{
  if (!holdingsleep(&b->sleeplock))
    panic("bwrite");
  virtio_disk_rw(b, 1);
}

// Release a locked buffer.
void brelse(struct buf *b)
{
  if (!holdingsleep(&b->sleeplock))
    panic("brelse");

  releasesleep(&b->sleeplock);

  acquire(&b->spinlock);
  b->refcnt--;
  if (b->refcnt == 0)
  {
    acquire(&tickslock);
    b->timestamp = ticks;
    release(&tickslock);
  }
  release(&b->spinlock);
}

void bpin(struct buf *b)
{
  acquire(&b->spinlock);
  b->refcnt++;
  release(&b->spinlock);
}

void bunpin(struct buf *b)
{
  acquire(&b->spinlock);
  b->refcnt--;
  release(&b->spinlock);
}
```

