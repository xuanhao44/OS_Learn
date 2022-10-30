# MIT 6.S081 - Lab Lock Buffer cache (3) 哈希单项优化

之前总算是把 timestamp 单项优化写出来了，那么现在就来看看哈希的单项优化吧。

## 1 哈希桶、散列函数

```c
// given number to hash
#define NBUCKETS 13

struct bucket
{
  struct spinlock lock;

  // Linked list of **a bucket of** buffers, through prev/next.
  // Sorted by how recently the buffer was used.
  // head.next is most recent, head.prev is least.
  struct buf head;
};

struct
{
  // physical buf
  struct buf buf[NBUF];
  struct bucket bucket[NBUCKETS];
} bcache;

int hash(uint blockno)
{
  return blockno % NBUCKETS;
}
```

bcache 由 bucket 数组和 buf 数组构成，其中 buf[NBUF] 是 bucket[NBUCKETS] 的物理载体。

bucket 由一个自旋锁和一个双向循环链表的头结点构成。

哈希函数是简单的取模。本来应该需要根据 key 的数字进行判断（也就是 blockno），但是指导书直接给了 % 13 的提示，那就直接用咯。当然一般是选较大的素数的，所以 13 应该没啥问题。

## 2 思路

字段 `blockno` 是缓存的磁盘块号。

在 bread 时，bget 从 bread 那里拿来 blockno，然后要开始找对应 blockno 的缓存块。把 blockno 放在 hash(blockno) 的哈希桶中，这样在找的时候，我们会优先到 hash(blockno) 的哈希桶中寻找；如果未命中，就需要找一个空闲块来替换了。我们的选择是 LRU 的块，即 least recent unused 块，我们先在当前的桶内寻找，如果连 unused 块都找不到，我们就去其他 bucket 找，找到了，那么我们就替换，并且把这个缓存块移动到 hash(blockno) 的哈希桶的桶中，放在 head.next 处。

在 brelse 中，我们会把上层文件系统释放的缓存块，取得这个块的 blockno，如果 refcnt - 1 后为 0，则将其又放入 hash(blockno) 的哈希桶中。

思考一个过程：一开始的时候并没有任何磁盘块的缓存块，上层文件系统调用 bread，然后调用 bget，先在 hash(blockno) 的哈希桶中寻找——当然无法命中；未命中，于是我们先在 hash(blockno) 的哈希桶中找 LRU 块，如果还是找不到，就去其他桶去找 LRU，找到了就替换，同时把这个缓存块移动到 hash(blockno) 的哈希桶的桶中，放在 head.next 处。

通过上面的思考还能知道几点：

1. 我们总是在尽力维护这些哈希桶，使得 blockno 的哈希函数值相同的缓存块都被放在 hash(blockno) 的哈希桶中。这样在查找命中的时候，我们就可以认为：如果我们需要查找 blockno 的缓存块，那么我应该先去 hash(blockno) 的哈希桶中找。至于维护的操作就是：brelse 的时候我们把 blockno 的缓存块又放回到 hash(blockno) 的哈希桶中，以及在未命中时，如果在其他桶中找到 LRU 块并替换之后，我们是会把这个块移动到 hash(blockno) 的哈希桶中的。
2. 我们知道当 $f(key1) = f(key2)$ 的时候，我们称这个散列函数发生了冲突。处理冲突的方式某一种方式是：链地址法。

> 将所有关键字为同义词的记录存储在一个单链表中，我们称这种表为同义词子表，在散列表中只存储所有同义词子表的头指针。对于关键字集合｛12，67，56，16，25，37，22，29，15，47，48，34｝，我们用前面同样的 12 为除数，进行除留余数法，可得到如右图结构，此时，已经不存在什么冲突换址的问题，无论有多少个冲突，都只是在当前位置给单链表增加结点的问题。
> 链地址法对于可能会造成很多冲突的散列函数来说，提供了绝不会出现找不到地址的保障。当然，这也就带来了查找时需要遍历单链表的性能损耗。

道理差不多，当然因为要找 LRU，所以我们现在用的是双向循环链表，头节点而不是头指针。同时这也能说明为什么桶的结构不用数组。

*另外，这个思路还有两个漏洞，在下面会提到，参见 6 思路漏洞。*

## 3 数据结构的一些操作

```c
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
```

看过原始的设计就知道双向循环链表的处理有多难看，还是把这些从函数中抽离出来吧。

## 4 优化实现

### 4.1 `binit`

```c
void binit(void)
{
  struct bucket *buk;
  struct buf *b;

  for (buk = bcache.bucket; buk < bcache.bucket + NBUCKETS; buk++)
  {
    // init bucket lock
    initlock(&buk->lock, "bcache.bucket");
    // Create linked list of buffers
    list_init(&buk->head);
  }

  // init 的时候就全部放到 bucket0 中
  buk = &bcache.bucket[0];

  for (b = bcache.buf; b < bcache.buf + NBUF; b++)
  {
    add_head(&buk->head, b);
    initsleeplock(&b->lock, "buffer");
  }
}
```

和原始的设计类似，我们把锁初始化，同时也把所有链表初始化。把所有的 buf 先放到 bucket0 中，当然也可以选择把 buf 均分到所有 bucket 中，写起来比这个复杂一点点。

```c
void binit(void)
{
  struct bucket *buk;
  struct buf *b;

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
```

个人认为把 buf 尽量均分到所有 bucket 中会比较好。

### 4.2 `brelse`

思路中提到过，我们把 unused 的缓存块又放回到 hash(blockno) 的哈希桶中。

```c
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
    remove(b);
    add_head(&buk->head, b);
  }
  release(&buk->lock);
}
```

### 4.3 `bpin` 和 `bunpin`

没有之前的 `bcache.lock`，现在就锁上 bucket 的锁就行了。

```c
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

### 4.4 `bget`

逻辑参考 2 思路部分。

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

  // we cannot find unused buf in the hash(blockno) bucket
  // search in other bucket
  for (buk = bcache.bucket; buk < bcache.bucket + NBUCKETS; buk++)
  {
    // do not search hash(blockno) bucket again
    if (buk == buk_key)
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
  
  release(&buk_key->lock);

  panic("bget: no buffers");
}
```

## 5 测试

能通过所有测试。

下面只放出 bcachetest，usertests 略。

*发现 usertests 会卡在 test manywrites 很久，不知道有没有问题。*

```c
$ bcachetest
start test0
test0 results:
--- lock kmem/bcache stats
lock: kmems0: #fetch-and-add 0 #acquire() 32950
lock: kmems1: #fetch-and-add 0 #acquire() 41
lock: kmems3: #fetch-and-add 0 #acquire() 37
lock: kmems4: #fetch-and-add 0 #acquire() 34
lock: kmems5: #fetch-and-add 0 #acquire() 7
lock: kmems6: #fetch-and-add 0 #acquire() 10
lock: kmems7: #fetch-and-add 0 #acquire() 30
lock: bcache.bucket: #fetch-and-add 0 #acquire() 4120
lock: bcache.bucket: #fetch-and-add 0 #acquire() 4120
lock: bcache.bucket: #fetch-and-add 0 #acquire() 2270
lock: bcache.bucket: #fetch-and-add 0 #acquire() 4276
lock: bcache.bucket: #fetch-and-add 0 #acquire() 2264
lock: bcache.bucket: #fetch-and-add 0 #acquire() 4264
lock: bcache.bucket: #fetch-and-add 0 #acquire() 4738
lock: bcache.bucket: #fetch-and-add 0 #acquire() 6680
lock: bcache.bucket: #fetch-and-add 0 #acquire() 8650
lock: bcache.bucket: #fetch-and-add 0 #acquire() 6174
lock: bcache.bucket: #fetch-and-add 0 #acquire() 6174
lock: bcache.bucket: #fetch-and-add 0 #acquire() 6180
lock: bcache.bucket: #fetch-and-add 0 #acquire() 6176
--- top 5 contended locks:
lock: proc: #fetch-and-add 786587 #acquire() 217625
lock: proc: #fetch-and-add 624688 #acquire() 217580
lock: virtio_disk: #fetch-and-add 545181 #acquire() 1205
lock: proc: #fetch-and-add 399190 #acquire() 217584
lock: proc: #fetch-and-add 370029 #acquire() 217584
tot= 0
test0: OK
start test1
test1 OK
```

## 6 思路漏洞

1. 死锁

需要注意的是，当我们去其他 bucket 中找 LRU 块的时候，我们是带着 hash(blockno) 的哈希桶的锁的。

首先我们不能再把自己找一遍，或者说，我们为什么要先去 hash(blockno) 的哈希桶找，就是为了和到其他 bukcet 找分开处理。在已经 acquire 一个锁之后，不能再 acquire 一次同一个锁。

其次，我们发现一个隐藏的问题——死锁。若我们持有 hash(blockno) 的哈希桶的锁，然后给其他桶上锁来查找；此时，如果另一个进程正好持有之前的某个其他桶的锁，然后向之前的 hash(blockno) 的哈希桶找，也就是尝试上锁的话，就有发生死锁的可能。

很遗憾的是，对于这个死锁问题，我暂时没找出改进的方法。

2. 并非真正的 LRU

我们所说的 LRU，是 least recent unused，在原本的设计中，单个链表所维护的 least recent，一定是所有 buf 中的 least recent，那么这个时候就只需要找 unused 就可以了。

但是现在我们使用了很多哈希桶，并在每个桶内部有一个 least recent 的有序结构。这还是所有 buf 中的 least recent 吗？并不是。那么有办法通过改进做到这一点吗，也不能。

我们现在的实现，LRU 只实现了 unused，这只能说是不会出错的。全局意义的 least recent，仅使用哈希桶的优化是做不到的。

## 7 最终代码

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

  // Linked list of **a bucket of** buffers, through prev/next.
  // Sorted by how recently the buffer was used.
  // head.next is most recent, head.prev is least.
  struct buf head;
};

struct
{
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

  // we cannot find unused buf in the hash(blockno) bucket
  // search in other bucket
  for (buk = bcache.bucket; buk < bcache.bucket + NBUCKETS; buk++)
  {
    // do not search hash(blockno) bucket again
    if (buk == buk_key)
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
  
  release(&buk_key->lock);

  panic("bget: no buffers");
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
    remove(b);
    add_head(&buk->head, b);
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

## 8 总结

哈希单项优化可以通过测试，但是哈希单项优化从理论上说并不存在真正的 LRU，且本篇中的实现没有解决潜在的死锁问题。

我们把进一步优化的希望寄托于同时使用两项优化。
