# MIT 6.S081 - Lab Page tables - A kernel page table per process

## 0 起始

任务二：

分理出独立页表。虚实地址相同的映射应该要保留，**先不需要**加上用户页表的内容。也就是说，当前任务下我们为每个进程创建的内核页表对应的地址空间大概包括：一些直接映射、`TRAMPOLINE`，以及内核栈。

## 1 流程

### 1.1 修改 `kernel/proc.h` 定义

修改 `kernel/proc.h` 中的 `struct proc`，增加两个新成员，分别用于给每个进程中设置一个内核独立页表和内核栈的物理地址。

```c
pagetable_t k_pagetable;     // kernel page table per process
uint64 kstack_pa;            // kstack physical address
```

下面将称原来的内核页表为 `kernel_pagetable`，每个进程的内核页表（A kernel page table per process）为 `k_pagetable`。

### 1.2 创建并初始化 `k_pagetable` 的函数

仿照 `kvminit()` 和 `kvmmap()` 函数重新写一个创建并初始化 `k_pagetable` 的函数。不修改 `kernel_pagetable`，而是直接创建一个新的内核页表，并将其返回。

下面是模仿来的 `ukvminit()` 和 `ukvmmap()`。

- 实现的时候不要映射 `CLINT`，否则会在任务三发生地址重合问题。
- 页表的创建也可使用原有的函数 `uvmcreate()`，如何实现看你喜好。

```c
// Allocate per process kernel page table.
// return this page table.
pagetable_t ukvminit()
{
  // kernel_pagetable = (pagetable_t)kalloc();
  // memset(kernel_pagetable, 0, PGSIZE);
  pagetable_t k_pagetable = uvmcreate();
  if (k_pagetable == 0)
    return 0;

  // uart registers
  ukvmmap(k_pagetable, UART0, UART0, PGSIZE, PTE_R | PTE_W);

  // virtio mmio disk interface
  ukvmmap(k_pagetable, VIRTIO0, VIRTIO0, PGSIZE, PTE_R | PTE_W);

  // 不要映射 CLINT, 否则会在任务三发生地址重合问题.
  // CLINT
  // ukvmmap(k_pagetable, CLINT, CLINT, 0x10000, PTE_R | PTE_W);

  // PLIC
  ukvmmap(k_pagetable, PLIC, PLIC, 0x400000, PTE_R | PTE_W);

  // map kernel text executable and read-only.
  ukvmmap(k_pagetable, KERNBASE, KERNBASE, (uint64)etext - KERNBASE, PTE_R | PTE_X);

  // map kernel data and the physical RAM we'll make use of.
  ukvmmap(k_pagetable, (uint64)etext, (uint64)etext, PHYSTOP - (uint64)etext, PTE_R | PTE_W);

  // map the trampoline for trap entry/exit to
  // the highest virtual address in the kernel.
  ukvmmap(k_pagetable, TRAMPOLINE, (uint64)trampoline, PGSIZE, PTE_R | PTE_X);

  return k_pagetable;
}

// add a mapping to the per process kernel page table.
// does not flush TLB or enable paging.
void ukvmmap(pagetable_t pagetable, uint64 va, uint64 pa, uint64 sz, int perm)
{
  if (mappages(pagetable, va, sz, pa, perm) != 0)
    panic("ukvmmap");
}
```

### 1.3 修改 `procinit()`、`allocproc()` 函数

原本的 `procinit()` 仅在 `kernel/main.c` 中被调用一次，用于给进程分配内核栈的物理页并在 `kernel_pagetable` 上建立映射。

申请来的物理页分配给内核栈，把物理地址放到 `kstack_pa` 中。

```c
char *pa = kalloc();
if (pa == 0)
   panic("kalloc");
p->kstack_pa = (uint64)pa;
```

同时还需要**保留**内核栈在 `kernel_pagetable` 上的映射。

```c
uint64 va = KSTACK((int)(p - proc));
p->kstack = va;
kvmmap(p->kstack, p->kstack_pa, PGSIZE, PTE_R | PTE_W);
```

现在 `kstack` 中保存了内核栈的虚拟地址，`kstack_pa` 中保存了内核栈的物理地址。由于 `k_pagetable` 还没有被创建，所以我们等到修改 `allocproc()` 函数时再来处理这个映射关系。

`allocproc()` 会在系统启动时被第一个进程和 `fork` 调用。

在 `allocproc()` 函数里，我们调用 `ukvminit()` 创建和初始化 `k_pagetable`。

```c
// A process kernel page table.
p->k_pagetable = ukvminit();
if (p->k_pagetable == 0)
{
  freeproc(p);
  release(&p->lock);
  return 0;
}
```

调用 `ukvmmap()` 将上面的映射放到 `k_pagetable` 里。

```c
ukvmmap(p->k_pagetable, p->kstack, p->kstack_pa, PGSIZE, PTE_R | PTE_W);
```

### 1.4 修改 `scheduler()` 函数

修改调度器，使得**切换进程的时候切换内核页表**。在进程切换的同时也要切换页表将其放入 SATP 寄存器中。

借鉴 `kvminithart()` 的页表载入方式，我们在 `kernel/vm.c` 中添加函数：

```c
// Switch h/w page table register to the process kernel page table,
// and enable paging.
void ukvminithart(pagetable_t k_pagetable)
{
  w_satp(MAKE_SATP(k_pagetable));
  sfence_vma();
}
```

**无进程运行的适配** ：当目前没有进程运行的时候，`scheduler()` 应该要 SATP 载入 `kernel_pagetable`。

最后 `scheduler()` 修改为：（展示部分）

```c
// Switch to chosen process.  It is the process's job
// to release its lock and then reacquire it
// before jumping back to us.
p->state = RUNNING;
c->proc = p;

// 在进程切换的同时也要切换进程内核页表将其放入寄存器 satp 中
ukvminithart(p->k_pagetable);

swtch(&c->context, &p->context);

// Process is done running for now.
// It should have changed its p->state before coming back.

// 当目前没有进程运行的时候
// 应该要 satp 载入全局的内核页表 kernel_pagetable
kvminithart();

c->proc = 0;

found = 1;
```

### 1.5 修改 `freeproc()` 函数

当进程结束的时候，你需要修改 `freeproc()` 函数来**释放对应的内核页表**。

你需要找到**释放页表但不释放叶子页表指向的物理页帧**的方法。

`kernel/proc.c:freeproc()`

```c
static void
freeproc(struct proc *p)
{
  if (p->trapframe)
    kfree((void *)p->trapframe);
  p->trapframe = 0;

  if (p->pagetable)
    proc_freepagetable(p->pagetable, p->sz);
  p->pagetable = 0;

  if (p->k_pagetable)
    ukfreewalk(p->k_pagetable);
  p->k_pagetable = 0;

  p->sz = 0;
  p->pid = 0;
  p->parent = 0;
  p->name[0] = 0;
  p->chan = 0;
  p->killed = 0;
  p->xstate = 0;
  p->state = UNUSED;
}
```

添加函数：`kernel/vm.c:ukfreewalk()`

```c
// Recursively free page-table pages
// but retain leaf physical addresses
void ukfreewalk(pagetable_t pagetable)
{
  for (int i = 0; i < 512; i++)
  {
    pte_t pte = pagetable[i];
    if ((pte & PTE_V) && (pte & (PTE_R | PTE_W | PTE_X)) == 0)
    {
      uint64 child = PTE2PA(pte);
      ukfreewalk((pagetable_t)child);
    }
    // leaf: do nothing

    // no matter leaf or not
    pagetable[i] = 0;
  }
  kfree((void *)pagetable);
}
```

## 2 原理 & 细节讲解

### 2.1 内核栈物理地址 `kstack_pa`

走完上面的流程就会发现 `kstack_pa` 的设置并不是必要的，而只是一种优化方法。记录了内核栈物理地址，我们就可以在之后避免通过 `kstack` 这个虚拟地址来寻找物理地址。

### 2.2 内核栈的分配以及设置映射

内核栈 `kstack` 可以在 `procinit()` 里被分配内存，在 `allocproc()` 中设置映射，上面的实现就是这样。

但是也可以这样：`procinit()` 中没有操作，`kstack` 在 `allocproc()` 里被分配内存并设置映射。

这两种方法会导致 `freeproc()` 产生差别：

显然，`procinit()` 只会在 xv6 启动的时候调用一次，而 `allocproc()` 至少在 `fork` 的时候就会被调用。

`freeproc()` 总是和 `allocproc()` 对应的，它们就是一个进程的开始和结束时会被调用的函数。功能也是对应的。所以如果 `kstack` 在 `allocproc()` 里被分配内存并设置映射，我们就有必要在 `freeproc()` 中释放这些内存并解除映射。

显然，`kstack` 的映射随着 `k_pagetable` 的清除 & 释放而被解除，不需要专门来解除 `kstack` 的映射，但我们需要释放 `kstack_pa`。

### 2.3 是否需要保留内核栈在 `kernel_pagetable` 上的映射

上面的实现中，所有内核栈都在 `kernel_pagetable` 中，同时每个 `k_pagetable` 也都持有自己进程的内核栈。

保留内核栈在 `kernel_pagetable` 是可行的。在 `procinit()` 里每次为内核栈申请物理页时记录一下 va 和 pa，然后建立在 `kernel_pagetable` 上的映射。

```c
char *pa = kalloc();
if (pa == 0)
  panic("kalloc");
p->kstack_pa = (uint64)pa;

uint64 va = KSTACK((int)(p - proc));
p->kstack = va;

kvmmap(p->kstack, p->kstack_pa, PGSIZE, PTE_R | PTE_W);
```

还有一种做法：不保留内核栈在 `kernel_pagetable` 上的映射，仅使用分散在每个 `k_pagetable` 的内核栈。

于是我们注释掉上面 `kvmmap() `的部分。但是在编译 xv6 的时候却遇到了问题：

```shell
hart 1 starting
hart 2 starting
panic: kvmpa
```

查看 `kvmpa()` 函数及其用法，发现位于 `kernel/vm.c`，仅被用于 `kernel/virtio_disk.c`。

`kernel/vm.c:kvmpa()`

```c
// translate a kernel virtual address to
// a physical address. only needed for
// addresses on the stack.
// assumes va is page aligned.
uint64 kvmpa(uint64 va)
{
  uint64 off = va % PGSIZE;
  pte_t *pte;
  uint64 pa;

  pte = walk(kernel_pagetable, va, 0);
  if (pte == 0)
    panic("kvmpa");
  if ((*pte & PTE_V) == 0)
    panic("kvmpa");
  pa = PTE2PA(*pte);
  return pa + off;
}
```

`kernel/virtio_disk.c`（截取部分）

```c
// buf0 is on a kernel stack, which is not direct mapped,
// thus the call to kvmpa().
disk.desc[idx[0]].addr = (uint64) kvmpa((uint64) &buf0);
```

可以看到，`kvmpa()` 只翻译了一下内核栈地址，并且 `walk()` 的参数是 `kernel_pagetable`。那么答案就很明显了：我们若保留内核栈在 `kernel_pagetable` 上的映射，那么这个 `walk()` 就还能正常的运作；但如果不保留的话，`kvmpa`() 就不会在 `kernel_pagetable` 上找到需要的内核栈，自然会 panic。

所以我们如果想要不保留内核栈在 `kernel_pagetable` 上的映射的话，就需要修改 `kvmpa()` 函数。

```c
uint64 kvmpa(uint64 va)
{
  uint64 off = va % PGSIZE;
  pte_t *pte;
  uint64 pa;

  pte = walk(myproc()->k_pagetable, va, 0);
  if (pte == 0)
    panic("kvmpa");
  if ((*pte & PTE_V) == 0)
    panic("kvmpa");
  pa = PTE2PA(*pte);
  return pa + off;
}
```

这样在调用 `kvmpa()` 时，函数就能在正确的页表上找到内核栈了。

*需要在末尾添加头文件 #include "spinlock.h" 和 #include "proc.h" 以使用 `myproc()`*

### 2.4 内核栈的虚拟地址的设置

上面的流程中，我们给内核栈的虚拟地址是 `p->kstack = KSTACK((int)(p - proc)) `，这是因为内核栈在内核地址空间上需要如此排列。我们在改造时就继承了这个做法，在用户地址空间中，我们仍然这样给予内核栈虚拟地址。

但是实际上直接套用是否好呢？有别的办法吗？其实是有的——**既然所有的内核栈不全部在内核地址空间中，而是分散到各个进程的用户地址空间中，那么我们干脆把内核栈在用户地址空间的虚拟地址就设置为在 `TRAMPOLINE` 之下**。

**同时需要注意，这样修改的前提是不保留内核栈在 `kernel_pagetable` 上的映射。**

于是在 2.3 的基础上，我们在 `kernel/memlayout.h` 中修改定义，添加 `UKSTACK`：

和 `KSTACK(p)` 对比就能看出区别。

```c
// map kernel stacks beneath the trampoline,
// each surrounded by invalid guard pages.
#define KSTACK(p) (TRAMPOLINE - ((p)+1)* 2*PGSIZE)
#define UKSTACK (TRAMPOLINE - 2*PGSIZE)
```

那么在 `allocproc()` 里，内核栈的虚拟地址就设置为 `UKSTACK`：

```C
// procinit 删除 p->kstack = va;
p->kstack = UKSTACK;

// allocproc
// 把内核栈映射到进程的内核页表里.
ukvmmap(p->k_pagetable, p->kstack, p->kstack_pa, PGSIZE, PTE_R | PTE_W);
```

事实证明这样是可行的。能做出这样的改进也说明我们对地址空间的认识是比较清楚的。

### 2.5 `ukfreewalk()` 函数

如何解除所有的映射，同时不释放物理页帧？一个很朴素的想法是用原有的函数来实现。

我们观察函数 `proc_freepagetable`：

```c
void proc_freepagetable(pagetable_t pagetable, uint64 sz)
{
  uvmunmap(pagetable, TRAMPOLINE, 1, 0);
  uvmunmap(pagetable, TRAPFRAME, 1, 0);
  uvmfree(pagetable, sz);
}
```

`uvmunmap()` 解除 `TRAMPOLINE` 和 `TRAPFRAME` 的映射。

`uvmfree()`

```c
// Free user memory pages,
// then free page-table pages.
void uvmfree(pagetable_t pagetable, uint64 sz)
{
  if (sz > 0)
    uvmunmap(pagetable, 0, PGROUNDUP(sz) / PGSIZE, 1);
  freewalk(pagetable);
}
```

用户进程所使用空间的大小是 sz，地址范围是 0 - sz。

`uvmunmap()` 的作用是把 `PGROUNDUP(sz) / PGSIZE` 数量的页帧清除掉，且叶子页表的 PTE 也被清除；`freewalk()` 的作用就是清除掉剩下的映射。

乍一看可以借鉴的很多，但实际上并不是这样。

1.  内核页表没有 `TRAPFRAME`，不需要解除。
2. `uvmunmap()` 实质上是从给定 va 位置开始释放 n 页物理页帧，所以在清除用户进程地址空间的时候，我们很清楚要把参数设置为 0, `PGROUNDUP(sz) / PGSIZE`。但是内核地址空间并没有这样比较简单的内存使用——至少那些直接映射都是很稀疏的分散在低地址处，所以 `uvmunmap()` 也用不上。

   *至于最开始为啥看上这个函数，是因为参数 `do_free` 设置为 0 就是不释放物理页帧，这还是很有吸引力的。（笑）*
3. 使用 `freewalk()` 的前提是叶子页表的 PTE 被清除。但我们连 `uvmunmap()` 都不用了，硬要套用 `freewalk()` 也就更奇怪了。

所以干脆我们自己写一个吧！定制的符合我们的需求。

遍历页表的时候，递归到叶子页表就停止；不管是不是叶子页表，我们都释放掉。

*有没有觉得之前的思考太过复杂（乐）*

```c
// Recursively free page-table pages
// but retain leaf physical addresses
void ukfreewalk(pagetable_t pagetable)
{
  for (int i = 0; i < 512; i++)
  {
    pte_t pte = pagetable[i];
    if ((pte & PTE_V) && (pte & (PTE_R | PTE_W | PTE_X)) == 0)
    {
      uint64 child = PTE2PA(pte);
      ukfreewalk((pagetable_t)child);
    }
    // leaf: do nothing

    // no matter leaf or not
    pagetable[i] = 0;
  }
  kfree((void *)pagetable);
}
```

## 3 后记

2.1、2.2、2.3 分别说的是三个不同的事，请注意区分；2.3 和 2.4 有逻辑上的先后关系。前三个点是我写完之后总结出来的，在我写的过程中我基本上没有把这三个问题区分开——也就是说混成一团。这真是很糟糕的体验。

一个负面例子：[MIT 6.S081 Lecture Notes | Xiao Fan (樊潇) (fanxiao.tech)](https://fanxiao.tech/posts/MIT-6S081-notes/#38-lab-3-pgtbl)

他采用了 “`allocproc()` 分配并映射内核栈 + 取消内核栈在 `kernel_pagetable` 上的映射” 的方案，成功让我在参考他的代码的时候迷糊了很久。另外很离谱的是，他并没有提到他修改了 `kvmpa()` 函数，导致我尝试他的方案的时候一直失败，也就是说这个 blog 写的不太行。**并且他的任务三写的也有很大的问题。**
