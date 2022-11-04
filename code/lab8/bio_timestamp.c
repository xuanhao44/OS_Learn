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

  // stage1: 第一次检测, 99% 会命中在第一次检测
  // 但是没有 `bcache.lock` 保护, 它可能会抢走别人的
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
  // stage2
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

  uint time_least;
  struct buf *lrub;

LOOP:
  // Not cached.
  // **Recycle** the least recently used (LRU) unused buffer.
  time_least = 0xffffffff;
  lrub = (void *)0;
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

  if (lrub == (void *)0) // no unused buf, should panic
  {
    release(&bcache.lock);
    panic("bget: no buffers");
  }
  else if (lrub != (void *)0 && lrub->refcnt == 0) // unsafe check
  {
    // acquire lock
    acquire(&lrub->spinlock);
    // double check
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
  else // grab by other's stage1
  {
    // release(&bcache.lock); no need
    goto LOOP; // goto stage2
  }
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