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