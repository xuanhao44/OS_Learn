# MIT 6.S081 - Lab Page tables - Simplify copyin/copyinstr

## 1 原理

### 1.1 背景

xv6 目前使用 `kernel/vm.c` 中的 `copyin()/copyinstr()` 将用户地址空间的数据拷贝至内核地址空间，它们通过软件模拟翻译的方式获取用户空间地址对应的物理地址，然后进行复制。

### 1.2 任务三

**在独立内核页表加上用户地址空间的映射，同时将函数 `copyin()/copyinstr()` 中的软件模拟地址翻译改成直接访问** ，使得内核能够不必花费大量时间，用软件模拟的方法一步一步遍历页表，而是直接利用硬件。

1. **已经提供了**新的函数 `copyin_new()/copyinstr_new()` （在 `kernel/vmcopyin.c`中定义）。用新的函数代替原本的 `copyin()/copyinstr()`。
2. 在独立内核页表加上用户页表的映射，以保证刚刚替换的新函数能够使用。但是要注意**地址重合问题**。

### 1.3 计算机硬件 MMU

通常进行地址翻译的时候，计算机硬件（即内存管理单元 MMU）都会自动的查找对应的映射进行翻译。然而，在 xv6 内核需要翻译用户的虚拟地址时，因为内核页表不含对应的映射，计算机硬件不能自动帮助完成这件事。

当我们在内核页表中拥有了用户进程的映射后，内核就可以直接访问用户进程的虚拟地址。比如可以直接解引用用户地址空间中的指针，就能获取到对应地址的数据。很多系统调用，或者内核的一些操作，都需要进行虚实地址翻译，因此我们在拥有了用户进程的地址映射后，原有麻烦的软件模拟翻译就可以被去除，很多操作都可以被简化，性能也可以得到提升。

**但需要注意的是** ，如果直接把用户页表的内容复制到内核页表，即其中页表项的 User 位置 1，那么内核依旧无法直接访问对应的虚拟地址（硬件会拒绝地址翻译，但软件模拟翻译依旧可行）。

可供参考的解决方案有两种：

1. 把内核页表中页表项的 User 位均置为 0；

2. 借助 RISC-V 的 `sstatus` 寄存器，如果该寄存器的 SUM 位（第 18 位）置为 1，那么内核也可以直接访问上述的虚拟地址。大多情况下，该位需要置 0。

### 1.4 内核虚拟地址与用户虚拟地址不重合

用户程序从 0 **开始**，内核则从一个很高的虚拟地址（0x0C000000）开始**排布**。但是他们还是有可能发生重叠。所以一定要**限制用户程序虚拟地址与内核地址重叠**。

内核页表不是从 0 开始，而是在某些特定地址上有特定的映射。

- 0x0C000000 是 PLIC（Platform-Level Interrput Controller，中断控制器）的地址。

- 0x02000000 是 CLINT（Core Local Interruptor，本地中断控制器）的地址。

```c
// local interrupt controller, which contains the timer.
#define CLINT 0x2000000L
// qemu puts programmable interrupt controller here.
#define PLIC 0x0c000000L
```

0x02000000 小于 0x0C000000，这会导致重叠。所以我们只会在内核初始化的时候用到这段地址，**为用户进程生成内核页表的时候可以不必映射这段地址**。

用户页表是从虚拟地址 0 开始，用多少就建多少，但最高地址不能超过内核的起始地址，这样用户程序可用的虚拟地址空间就为 `0 - 0xC000000`。

![页表合并](https://typora-1304621073.cos.ap-guangzhou.myqcloud.com/typora/%E9%A1%B5%E8%A1%A8%E5%90%88%E5%B9%B6.png)

## 1 流程

### 1.1 把进程的用户页表映射到内核页表中的两个函数

`ukvmcopy()` 整体模仿 `uvmcopy()`。

改进：

1. 增加参数，`oldsz` 和 `newsz` 分别指需要被建立映射的，起始和结束的虚拟地址。
2. 删除复制物理内存的部分，我们只需要映射。
3. 需要把 PTE 的 Flags 进行处理，这样才能放到进程内核页表中。
4. 注意限制大小不超过 PLIC。

```c
int ukvmcopy(pagetable_t old, pagetable_t new, uint64 oldsz, uint64 newsz)
{
  pte_t *pte;
  uint64 pa, i;
  uint flags;

  // 防止 user virtual address 超过 PLIC
  if (PGROUNDUP(newsz) > PLIC)
    return -1;

  if (newsz < oldsz)
    return -1;

  oldsz = PGROUNDUP(oldsz);
  for (i = oldsz; i < newsz; i += PGSIZE)
  {
    if ((pte = walk(old, i, 0)) == 0)
      panic("ukvmcopy: pte should exist");
    if ((*pte & PTE_V) == 0)
      panic("ukvmcopy: page not present");
    pa = PTE2PA(*pte);
    // 清除原先 PTE 中的 PTE_U 标志位
    flags = PTE_FLAGS(*pte) & (~PTE_U);
    if (mappages(new, i, PGSIZE, (uint64)pa, flags) != 0)
    {
      // 注意 dofree 参数设置为 0, 我们只清理映射, 不清理物理内存
      uvmunmap(new, 0, i / PGSIZE, 0);
      return -1;
    }
  }

  return 0;
}
```

注意到 `ukvmcopy()` 仅是一种为了内存增量而设置映射的函数，那么当用户内存减少时，我们也要考虑同步清除对应的映射。

于是我们模仿 `uvmdealloc()` 写出 `ukvmdealloc()`。他与原函数唯一的区别就是 `uvmunmap()` 的 `do_free` 参数，我们改为 0 是因为不需要清理物理内存。

```c
uint64 ukvmdealloc(pagetable_t k_pagetable, uint64 oldsz, uint64 newsz)
{
  if (newsz >= oldsz)
    return oldsz;

  if (PGROUNDUP(newsz) < PGROUNDUP(oldsz))
  {
    int npages = (PGROUNDUP(oldsz) - PGROUNDUP(newsz)) / PGSIZE;
    uvmunmap(k_pagetable, PGROUNDUP(newsz), npages, 0);
  }

  return newsz;
}
```

*记得把新增的函数加入 `kernel/def.h`*

### 1.2 用新的函数代替原本的 `copyin()/copyinstr()`

```c
// Copy from user to kernel.
// Copy len bytes to dst from virtual address srcva in a given page table.
// Return 0 on success, -1 on error.
int copyin(pagetable_t pagetable, char *dst, uint64 srcva, uint64 len)
{
  return copyin_new(pagetable, dst, srcva, len);
}

// Copy a null-terminated string from user to kernel.
// Copy bytes to dst from virtual address srcva in a given page table,
// until a '\0', or max.
// Return 0 on success, -1 on error.
int copyinstr(pagetable_t pagetable, char *dst, uint64 srcva, uint64 max)
{
  return copyinstr_new(pagetable, dst, srcva, max);
}
```

*同样要记得加入 `kernel/def.h`*

### 1.3 修改 `fork()`、`exec()`、`growproc()` 和 `userinit()` 函数

在独立内核页表加上用户页表的映射的时候，每一次用户页表被修改了映射的同时，都要修改对应独立内核页表的相应部分保持同步。这通常在 `fork()`，`exec()`，`sbrk()` 中发生，其中 `sbrk()` 调用 `growproc()` 来实现内存分配或回收。也就是需要在 `fork()`、`exec()` 和 `growproc()` 这三个函数里将改变后的进程页表同步到内核页表中。

注意：

第一个进程也需要将用户页表映射到内核页表中，见 `kernel/proc.c: userinit()`。

在 `p->sz` 被设置好了之后我们就复制用户进程页表。

```c
ukvmcopy(p->pagetable, p->k_pagetable, 0, p->sz);
```

在改进其他函数之前需要先理清这个函数的流程，不然改动就无从说起。

#### 1.3.1 `fork()`

`fork()` 整个函数的作用不必多说，在内存以及页表方面，原本的操作是：

```c
  // Copy user memory from parent to child.
  if (uvmcopy(p->pagetable, np->pagetable, p->sz) < 0)
  {
    freeproc(np);
    release(&np->lock);
    return -1;
  }
  np->sz = p->sz;
```

没有对内存的大小以及页表做出什么很大的改动，就是把内存以及页表都复制了一份——这是很合理的，毕竟父子进程使用的不是同一份内存，比如对变量的值的修改是不相关的。

我们要做的就是在下面加一句话：普普通通的把页表复制过来就行了。

```c
ukvmcopy(np->pagetable, np->k_pagetable, 0, np->sz);
```

#### 1.3.2 `exec()`

`exec()` 前面的大半部分都在设置 pagetable，在最后 commit 的时候再交给 `p->pagetable`。

```c
  // Commit to the user image.
  oldpagetable = p->pagetable;
  p->pagetable = pagetable;
  p->sz = sz;
  p->trapframe->epc = elf.entry; // initial program counter = main
  p->trapframe->sp = sp;         // initial stack pointer
  proc_freepagetable(oldpagetable, oldsz);
```

我们需要做的就是完成处理旧的进程内核页表，以及复制出新的进程内核页表。

```c
  // Commit to the user image.
  oldpagetable = p->pagetable;
  p->pagetable = pagetable;
  // 处理旧的进程内核页表
  ukvmdealloc(p->k_pagetable, p->sz, 0);
  // 复制出新的进程内核页表
  if (ukvmcopy(p->pagetable, p->k_pagetable, 0, sz) < 0)
    goto bad;
  p->sz = sz;
  p->trapframe->epc = elf.entry; // initial program counter = main
  p->trapframe->sp = sp;         // initial stack pointer
  proc_freepagetable(oldpagetable, oldsz);
```

需要注意的是，不需要模仿 `exec()` 那样先设置新的 pagetable，最后转交结束后再释放 oldpagetable。我们直接先清除旧的进程内核页表，复制出新的进程内核页表。

### 1.3.3 `growproc()`

`growproc()` 涉及到增加内存和减少内存。这也是我们为什么要把 `ukvmcopy()` 的参数增加的原因——不仅仅是只从 0 开始了。

那么在扩大内存时，我们使用 `ukvmcopy()` 同步复制新的映射；在缩小内存时，我们使用 `ukvmdealloc()` 消除对应映射。

```c
// Grow or shrink user memory by n bytes.
// Return 0 on success, -1 on failure.
int growproc(int n)
{
  uint sz;
  struct proc *p = myproc();

  sz = p->sz;
  if (n > 0)
  {
    if ((sz = uvmalloc(p->pagetable, sz, sz + n)) == 0)
      return -1;

    if (ukvmcopy(p->pagetable, p->k_pagetable, p->sz, sz) != 0)
      return -1;
  }
  else if (n < 0)
  {
    sz = uvmdealloc(p->pagetable, sz, sz + n);
    ukvmdealloc(p->k_pagetable, p->sz, p->sz + n);
  }
  p->sz = sz;
  return 0;
}
```

## 2 改进以及一些其他理解

### 2.1 关于 `ukvmcopy()` 实现

虽然上面的流程提到是模仿 `uvmcopy()`，但是实际上有更简单的方法。

我们没有必要使用 `mappages()`——它只不过是 `walk()` 的套壳罢了，我们没有必要使用它，我们可以直接使用 `walk()`。

我们使用 `walk()` 在旧页表中，用虚拟地址 `i` 抓出对应的叶子页表上的 PTE。那么从旧页表里抓出来的 PTE 我们检查一下 Valid 位再使用。这和之前一致。

之后我们拿着这个虚拟地址 `i` 在新页表中建立映射，也就是给它找一个 PTE，这个过程中页表也被建立。但是需要注意的是，用 `walk()` 新设置的 PTE 的 Flags 是没有设置好的，我们去看 `mappages ` 也知道需要给它添上 Valid 位。所以如果再像之前那样 `walk()` 完就检查 Valid 位就必定 panic。因此我们只需要修改这个 PTE，将 User 位置零就好了。

```c
int ukvmcopy(pagetable_t old, pagetable_t new, uint64 oldsz, uint64 newsz)
{
  pte_t *pte_from, *pte_to;
  uint64 i;

  // 防止 user virtual address 超过 PLIC
  if (PGROUNDUP(newsz) > PLIC)
    return -1;

  if (newsz < oldsz)
    return -1;

  oldsz = PGROUNDUP(oldsz);
  for (i = oldsz; i < newsz; i += PGSIZE)
  {
    if ((pte_from = walk(old, i, 0)) == 0)
      panic("ukvmcopy: pte_from pte should exist");
    if ((*pte_from & PTE_V) == 0)
      panic("ukvmcopy: pte_from page not present");
    
    // walk 参数设置为 1 是在需要时为映射创建页表
    if ((pte_to = walk(new, i, 1)) == 0)
      panic("ukvmcopy: walk fails");
    
    *pte_to = (*pte_from) & (~PTE_U);
  }

  return 0;
}
```

### 2.2 关于 PLIC 限制的处理

事实上，我认为 PLIC 的限制的处理应该放在 `uvmalloc()` 中，毕竟那里才是进程申请内存的地方。这个限制还可以被加到很多地方，就不多说了。

也许可以考虑在加载 ELF 的时候就会超出 PLIC（`exec()` 中的部分）？这也许都是可能的吧，我也不是很清楚。

### 2.3 `copyout()` 改造？

未写完。
