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