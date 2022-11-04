# MIT 6.S081 - Lab Lock Buffer cache (4) 使用两项优化

[OS_Learn/bio_mix.c at main · xuanhao44/OS_Learn (github.com)](https://github.com/xuanhao44/OS_Learn/blob/main/code/lab8/bio_mix.c)

【仅包含部分代码，不能运行请自行查看文章补充相关内容】

终于到最后的时刻了。我们将尝试使用两项优化。所有的原理和细节，之前的文章都有提到过，我们需要分析现有情况，来做出最好的选择。

## 1 思路

- 数据结构的设计为整体思路服务。
- `bpin`、`bunpin`、`bread` 和 `bwrite` 不是重点，不在本篇提及。
- `brelse`：学习时间戳优化优点，**只修改 buf 时间戳，不做链表元素的移动**，不在本篇提及。
- **重点** `bget`：查找命中，未命中找 LRU 替换。
  - 第一阶段 stage1 在自己的桶查找命中。
  - 第二阶段 stage2，加上 `bcache.lock` / 在 `bcache.lock ` 的保护下进行。
    - 再次在自己的桶查找命中，还若未命中就去**所有**桶去查找。
    - 得到的 LRU 进行乐观锁类似的检查，如果没拿到 LRU 块，则 panic，如果拿到了但是被抢走了，goto stage2 查找部分。
      - 真的拿到了就开始替换，替换结束，锁处理完毕，返回带睡眠锁的 buf。

## 2 数据结构设计

### 2.1 `kernel/buf.h`

```c
struct buf {
  struct sleeplock lock;
  int valid;   // has data been read from disk?
  int disk;    // does disk "own" buf?
  uint dev;
  uint blockno;
  uint refcnt;
  struct buf *prev;
  struct buf *next;
  uint timestamp;
  uchar data[BSIZE];
};
```

我们选择使用双向循环链表，配合上时间戳 `timestamp`。

链表的初始化、头插法，以及删除元素就和原先一样。

尽管有了 `timestamp`，我们不需要从 `head.prev` 开始向前遍历，但是我们仍然不选择单向链表。

原因是删除元素的操作不如双向链表方便——单向链表删除元素需要获得前驱，而前驱只能通过遍历获得。

### 2.2 `bucket`、`bcache`

```c
// given number to hash
#define NBUCKETS 13

struct bucket
{
  struct spinlock lock;
  struct buf head;
};

struct
{
  struct spinlock lock;
  // physical buf
  struct buf buf[NBUF];
  struct bucket bucket[NBUCKETS];
} bcache;
```

沿用之前哈希桶优化的设计，但是给 bcache 增加了自旋锁——这是为了保护搜索的结果不会因为没有锁的保护而失效。

没有整个锁保护的 bcache，实际是很脆弱的，无法保证查找 LRU 和替换的过程有原子性。我们需要这样一个 `bcache.lock`，来保证查找 LRU 和替换的过程是原子的。（虽然最后不一定达到了这个目的）

## 3 `bget`

### 3.1 - 第一阶段 stage1 在自己的桶查找命中

```c
// temp
  struct buf *b;
  struct bucket *buk;

  // not change!
  struct bucket *buk_key = &bcache.bucket[hash(blockno)];

  // stage1: search in buk_key, most of the time it will find cached
  // but it is unsafe, may grab buf from the other proc's stage 2
  acquire(&buk_key->lock);
  for (b = buk_key->head.next; b != &buk_key->head; b = b->next)
  {
    if (b->dev == dev && b->blockno == blockno)
    {
      b->refcnt++; // 该块被引用次数 ++
      release(&buk_key->lock);
      acquiresleep(&b->lock);
      return b;
    }
  }
  release(&buk_key->lock);
```

第一阶段没有加上 `bcache.lock`。这不算很安全的检测。这可能会导致第二阶段找到的 LRU 块被第一阶段抢走，我们之后处理这个问题。并且只能如此，因为如果第一阶段就加上大锁的话，锁的竞争就太多了。我们倾向于 ”等这种事情发生了我们再去处理“ 的做法。

### 3.2 - 再次在自己的桶查找命中

```c
acquire(&bcache.lock);

  // double check
  acquire(&buk_key->lock);
  for (b = buk_key->head.next; b != &buk_key->head; b = b->next)
  {
    if (b->dev == dev && b->blockno == blockno)
    {
      b->refcnt++; // 该块被引用次数 ++
      release(&buk_key->lock);
      release(&bcache.lock);
      acquiresleep(&b->lock);
      return b;
    }
  }
  release(&buk_key->lock);
```

（最开头有个获取 `bcache.lock` 的部分）

和 stage1 的区别就在于，这个过程已经处于 `bcache.lock` 的保护下了。可以看到它的 acquire 和 release。

当我们离开这个步骤的时候，当前进程只拥有 `bcache.lock` 这个锁。

### 3.3 -未命中就去**所有**桶去查找

```c
  uint time_least;
  struct buf *lrub;

LOOP:
  // Not cached.
  // search in all buckets.
  // need the true lru buf, which has the smallest timestamp.
  time_least = 0xffffffff;
  lrub = (void *)0;
  for (buk = bcache.bucket; buk < bcache.bucket + NBUCKETS; buk++)
  {
    acquire(&buk->lock);
    for (b = buk->head.next; b != &buk->head; b = b->next)
    {
      // refcnt == 0 means unused
      if (b->refcnt == 0 && b->timestamp < time_least)
      {
        time_least = b->timestamp;
        lrub = b;
      }
    }
    release(&buk->lock);
  }
```

（先不去管 LOOP）

整个搜索过程很普通，就是遍历每个桶，每个桶中遍历所有缓存块。

由于我们之前已经释放了 buk_key，所以我要强调是**所有桶——我们都要搜索**。

### 3.4 - 得到的 LRU 进行乐观锁类似的检查

大致结构我们在之前的文章提到过：[MIT 6.S081 - Lab Lock Buffer cache (2) timestamp 单项优化](https://www.sheniao.top/os/122.html)

> 1. 如果没有找到 lrub（lrub 为空），那么还是需要 panic 的。
> 2. 如果找到了 lrub（lrub 不为空），但是被第一阶段抢走了（refcnt 不等于 0），那么再找一遍。
> 3. 如果找到了 lrub（lrub 不为空），但是也没被第一阶段抢走（refcnt 等于 0），那么就确定了，可以开始替换。

我们需要注意的是：goto 的位置。

在本次实现中，如果我们选择 goto 之前不释放 `bcache.lock`。那么我们可以不把 LOOP 放到 stage2 的开头，而是把 LOOP 放到在所有锁中搜索 LRU 的遍历的开头。

下面的代码直接到了 `bget` 函数的末尾。

需要注意：如果 `lrub` 来自 buk_key，那么不要重复 acquire，和其他 bucket 区分开来。

```c
// at here, only bcache.lock is held by proc.

  // check if it was stolen by other proc's stage1
  if (lrub == (void *)0) // no unused buf, should panic
  {
    release(&bcache.lock);
    panic("bget: no buffers");
  }
  else if (lrub != (void *)0 && lrub->refcnt == 0) // unsafe check
  {
    buk = &bcache.bucket[hash(lrub->blockno)];
    // acquire lock
    acquire(&buk->lock);
    // double check
    if (lrub != (void *)0 && lrub->refcnt == 0)
    {
      lrub->dev = dev;
      lrub->blockno = blockno;
      lrub->valid = 0; // set valid 0, wait for virtio_disk_rw()
      lrub->refcnt = 1;

      // remove it from the buk
      // if comes from buk_key, do nothing
      if (buk != buk_key)
      {
        remove(lrub);
        acquire(&buk_key->lock);
        add_head(&buk_key->head, lrub);
        release(&buk_key->lock);
      }

      release(&buk->lock);
      release(&bcache.lock);
      acquiresleep(&lrub->lock);
      return lrub;
    }
    else
    {
      release(&buk->lock);
      // release(&bcache.lock); no need
      goto LOOP; // goto "stage2" search again
    }
  }
  else // grab by other's stage1
  {
    // release(&bcache.lock); no need
    goto LOOP; // goto "stage2" search again
  }
```

## 4 最终实现

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

// given number to hash
#define NBUCKETS 13

struct bucket
{
  struct spinlock lock;
  struct buf head;
};

struct
{
  struct spinlock lock;
  // physical buf
  struct buf buf[NBUF];
  struct bucket bucket[NBUCKETS];
} bcache;

int hash(uint blockno)
{
  return blockno % NBUCKETS;
}

// 初始化双向循环链表 (头结点)
void list_init(struct buf *head)
{
  head->next = head;
  head->prev = head;
}

// 头插法
// eg: add_head(&buk->head, b);
void add_head(struct buf *head, struct buf *b)
{
  b->next = head->next;
  b->prev = head;
  head->next->prev = b;
  head->next = b;
}

// 删除元素
void remove(struct buf *b)
{
  b->next->prev = b->prev;
  b->prev->next = b->next;
}

void binit(void)
{
  struct bucket *buk;
  struct buf *b;

  initlock(&bcache.lock, "bcache");

  for (buk = bcache.bucket; buk < bcache.bucket + NBUCKETS; buk++)
  {
    // init bucket lock
    initlock(&buk->lock, "bcache.bucket");
    // Create linked list of buffers
    list_init(&buk->head);
  }

  int i = 0;
  for (b = bcache.buf; b < bcache.buf + NBUF; b++)
  {
    buk = &bcache.bucket[i];
    i = (i + 1) % NBUCKETS;

    add_head(&buk->head, b);
    initsleeplock(&b->lock, "buffer");
  }
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf *
bget(uint dev, uint blockno)
{
  // temp
  struct buf *b;
  struct bucket *buk;

  // not change!
  struct bucket *buk_key = &bcache.bucket[hash(blockno)];

  // stage1: search in buk_key, most of the time it will find cached
  // but it is unsafe, may grab buf from the other proc's stage 2
  acquire(&buk_key->lock);
  for (b = buk_key->head.next; b != &buk_key->head; b = b->next)
  {
    if (b->dev == dev && b->blockno == blockno)
    {
      b->refcnt++; // 该块被引用次数 ++
      release(&buk_key->lock);
      acquiresleep(&b->lock);
      return b;
    }
  }
  release(&buk_key->lock);

  // stage2: use bcache.lock to protect the search
  acquire(&bcache.lock);

  // double check
  acquire(&buk_key->lock);
  for (b = buk_key->head.next; b != &buk_key->head; b = b->next)
  {
    if (b->dev == dev && b->blockno == blockno)
    {
      b->refcnt++; // 该块被引用次数 ++
      release(&buk_key->lock);
      release(&bcache.lock);
      acquiresleep(&b->lock);
      return b;
    }
  }
  release(&buk_key->lock);

  uint time_least;
  struct buf *lrub;

LOOP:
  // Not cached.
  // search in all buckets.
  // need the true lru buf, which has the smallest timestamp.
  time_least = 0xffffffff;
  lrub = (void *)0;
  for (buk = bcache.bucket; buk < bcache.bucket + NBUCKETS; buk++)
  {
    acquire(&buk->lock);
    for (b = buk->head.next; b != &buk->head; b = b->next)
    {
      // refcnt == 0 means unused
      if (b->refcnt == 0 && b->timestamp < time_least)
      {
        time_least = b->timestamp;
        lrub = b;
      }
    }
    release(&buk->lock);
  }

  // at here, only bcache.lock is held by proc.

  // check if it was stolen by other proc's stage1
  if (lrub == (void *)0) // no unused buf, should panic
  {
    release(&bcache.lock);
    panic("bget: no buffers");
  }
  else if (lrub != (void *)0 && lrub->refcnt == 0) // unsafe check
  {
    buk = &bcache.bucket[hash(lrub->blockno)];
    // acquire lock
    acquire(&buk->lock);
    // double check
    if (lrub != (void *)0 && lrub->refcnt == 0)
    {
      lrub->dev = dev;
      lrub->blockno = blockno;
      lrub->valid = 0; // set valid 0, wait for virtio_disk_rw()
      lrub->refcnt = 1;

      // remove it from the buk
      // if comes from buk_key, do nothing
      if (buk != buk_key)
      {
        remove(lrub);
        acquire(&buk_key->lock);
        add_head(&buk_key->head, lrub);
        release(&buk_key->lock);
      }

      release(&buk->lock);
      release(&bcache.lock);
      acquiresleep(&lrub->lock);
      return lrub;
    }
    else
    {
      release(&buk->lock);
      // release(&bcache.lock); no need
      goto LOOP; // goto "stage2" search again
    }
  }
  else // grab by other's stage1
  {
    // release(&bcache.lock); no need
    goto LOOP; // goto "stage2" search again
  }
}

// Return a locked buf with the contents of the indicated block.
struct buf *
bread(uint dev, uint blockno)
{
  struct buf *b;

  b = bget(dev, blockno);
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
  if (!holdingsleep(&b->lock))
    panic("bwrite");
  virtio_disk_rw(b, 1);
}

// Release a locked buffer.
// Move to the head of the most-recently-used list.
void brelse(struct buf *b)
{
  if (!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);

  struct bucket *buk = &bcache.bucket[hash(b->blockno)];

  acquire(&buk->lock);
  b->refcnt--;
  if (b->refcnt == 0)
  {
    // no one is waiting for it.
    // don't need to move to the head
    acquire(&tickslock);
    b->timestamp = ticks;
    release(&tickslock);
  }
  release(&buk->lock);
}

void bpin(struct buf *b)
{
  struct bucket *buk = &bcache.bucket[hash(b->blockno)];

  acquire(&buk->lock);
  b->refcnt++;
  release(&buk->lock);
}

void bunpin(struct buf *b)
{
  struct bucket *buk = &bcache.bucket[hash(b->blockno)];

  acquire(&buk->lock);
  b->refcnt--;
  release(&buk->lock);
}
```

