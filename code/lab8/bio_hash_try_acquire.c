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