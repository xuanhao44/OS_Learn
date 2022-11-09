# MIT 6.S081 - Lab Page tables -- vmprint

任务 1 就是让你熟悉 xv6 的源码的过程，所以不会太难；但你要用学到的理论知识去理解和熟悉代码。

## 1 MAXVA

```c
// one beyond the highest possible virtual address.
// MAXVA is actually one bit less than the max allowed by
// Sv39, to avoid having to sign-extend virtual addresses
// that have the high bit set.
#define MAXVA (1L << (9 + 9 + 9 + 12 - 1))
```

可以看到虚拟地址的最大值被设置为 38 位，不是我们说的 39 位，专门减去一。

## 2 va、pa、pte

- va：逻辑地址，应为 39 位，但是类型是 uint64
- pa：物理地址，应为 56 位，但是类型是 uint64
- pte_t：页表项，应为 44 + 10 = 54 位，但是类型是 uint64

```c
// shift a physical address to the right place for a PTE.
#define PA2PTE(pa) ((((uint64)pa) >> 12) << 10)
#define PTE2PA(pte) (((pte) >> 10) << 12)
```

可以看到 PTE 和 PA 的互相转化。

- PA 需要用右移来丢掉后 12 位，然后左移 10 位，从而变为 PTE；
- PTE 需要用右移来丢掉后 10 位，然后左移 11 位，从而变为 PA。

## 3 pagetable

pagetable_t：指针，指向页表的起始地址，但是类型是 uint64。

如何通过该地址读取页表？

```c
pte_t pte = pagetable[i];
```

可以看到使用类似于数组。里面存的东西是 pte。我们知道，pte 是物理地址的前 44 位，不管是某个页表或者是目标的物理地址。

所以如果要通过该 pte 获取物理地址，就会用到 PTE2PA，例如：

```
uint64 child = PTE2PA(pte);
```

但是在将其作为 pagetable_t 使用时需要强转成 pagetable_t，也可以理解为转为指针类型，例如：

```c
freewalk((pagetable_t)child);
```

## 4 `PX(level, va)`

用于获取 va 中对应页表的 9 位 index。

```c
#define PGSHIFT 12  // bits of offset within a page

...

// extract the three 9-bit page table indices from a virtual address.
#define PXMASK          0x1FF // 9 bits
#define PXSHIFT(level)  (PGSHIFT+(9*(level)))
#define PX(level, va) ((((uint64) (va)) >> PXSHIFT(level)) & PXMASK)
```

在 walk 函数中的使用：

```c
  for(int level = 2; level > 0; level--) {
    pte_t *pte = &pagetable[PX(level, va)];
    if(*pte & PTE_V) {
      pagetable = (pagetable_t)PTE2PA(*pte);
    } else {
      if(!alloc || (pagetable = (pde_t*)kalloc()) == 0)
        return 0;
      memset(pagetable, 0, PGSIZE);
      *pte = PA2PTE(pagetable) | PTE_V;
    }
  }
```

可以看到第一级页表对应的是 level 2。例如：

调用 `PX(1, va)`，那么就是计算 `((((uint64) (va)) >> (12 + (9*(1))) & 0x1FF)`，也就是逻辑地址右移 (12 + 9) 位，那么这样的话 L2（9 bit）就位于最后 9 位，与 0x1FF 相与，那么得到了正确的 L2。这样就可以作为 pte 数组的下标了。

## 5 Flags

### 5.1 from xv6 book

之前已经提到过 PTE 中的后 10 位的 Flags。

> Each PTE contains flag bits that tell the paging hardware how the associated virtual address is allowed to be used. PTE_V indicates whether the PTE is present: if it is not set, a reference to the page causes an exception (i.e. is not allowed). PTE_R controls whether instructions are allowed to read to the page. PTE_W controls whether instructions are allowed to write to the page. PTE_X controls whether the CPU may interpret the content of the page as instructions and execute them. PTE_U controls whether instructions in user mode are allowed to access the page; if PTE_U is not set, the PTE can be used only in supervisor mode. Figure 3.2 shows how it all works. The flags and all other page hardware-related structures are defined in (kernel/riscv.h)

定义：

```c
#define PTE_V (1L << 0) // valid
#define PTE_R (1L << 1)
#define PTE_W (1L << 2)
#define PTE_X (1L << 3)
#define PTE_U (1L << 4) // 1 -> user can access
```

提取 Flags：

```c
#define PTE_FLAGS(pte) ((pte) & 0x3FF)
```

实际在使用时有两种：

1. 按位与——只判断一种，其他不管

```c
if((pte & PTE_V) && (pte & (PTE_R|PTE_W|PTE_X)) == 0)
```

你可以看到 `pte & PTE_V` 就只判断了一种；

也可以像之前一样构造 mask 来判断。即 `pte & (PTE_R|PTE_W|PTE_X)`。

2. 确定的一种，排他性

```c
if(PTE_FLAGS(*pte) == PTE_V)
      panic("uvmunmap: not a leaf");
```

3. 确实是需要完整的 Flags

```c
flags = PTE_FLAGS(*pte);
```

*示例代码中 pte 类型不相同，请注意。*

### 5.2 from riscv-privileged

很多内容 xv6 book 并没有提到，比如叶子页表的判断，以及 SATP 寄存器的结构等等。

> The V bit indicates whether the PTE is valid; if it is 0, all other bits in the PTE are don’t-cares and may be used freely by software. The permission bits, R, W, and X, indicate whether the page is readable, writable, and executable, respectively. When all three are zero, the PTE is a pointer to the next level of the page table; otherwise, it is a leaf PTE. Writable pages must also be marked readable; the contrary combinations are reserved for future use.

注意到，当 PTE_R、PTE_W、PTE_X 都为 0 的时候，说明 PTE 所在页表“可以”指向下一个页表，而不是最终的物理地址，在 SV39 中，也就是第一二级页表；否则的话，这个页表就是叶子页表，在 SV39 中，也就是第三级页表。

所以在 `freewalk` 中有这样的判断：

```c
    if ((pte & PTE_V) && (pte & (PTE_R | PTE_W | PTE_X)) == 0)
    {
      ...
    }
    else if (pte & PTE_V)
    {
      ...
    }
```

可以看到，这就是利用 R W X 做了区分。

这三个 flag 还有更多组合，如图：

<img src="https://typora-1304621073.cos.ap-guangzhou.myqcloud.com/typora/PTE_R_W_X.png" alt="PTE_R_W_X" style="zoom:50%;" />

## 6 `freewalk`

经历了这么多，我们终于能来看看这个将被我们用来模仿的 `freewalk` 了。

很遗憾的是，网上的大多数教程不说他们是怎么从 `freewalk` 模仿出 `vmprint` 的。*我倾向于他们是蒙出来的。*

还有不用递归用迭代的，我觉得他们可能是模仿的 `walk`，这也不好，因为比较丑。

下面是加上了注释的 `freewalk`，先大致浏览一下。

```C
// Recursively free page-table pages.
// All leaf mappings must already have been removed.
void freewalk(pagetable_t pagetable)
{
  // there are 2^9 = 512 PTEs in a page table.
  // 遍历一个页表页的 PTE 表项
  for (int i = 0; i < 512; i++)
  {
    pte_t pte = pagetable[i]; // 获取第 i 条 PTE
    /*
     判断 PTE 的 Flag 位, 如果还有下一级页表(即当前是根页表或次页表),
     则递归调用 freewalk 释放页表项, 并将对应的 PTE 清零
    */
    if ((pte & PTE_V) && (pte & (PTE_R | PTE_W | PTE_X)) == 0)
    {
      // this PTE points to a lower-level page table.
      uint64 child = PTE2PA(pte);   // 将 PTE 转为为物理地址
      freewalk((pagetable_t)child); // 递归调用 freewalk
      pagetable[i] = 0;             // 清零
    }
    else if (pte & PTE_V)
    {
      /*
      如果叶子页表的虚拟地址还有映射到物理地址, 报错 panic
      因为调用 freewalk 之前应该会先 uvmunmap 释放物理内存
      */
      panic("freewalk: leaf");
    }
  }
  kfree((void *)pagetable); // 释放 pagetable 指向的物理页
}
```

首先需要注意的两点：

1. `freewalk` 被调用的前提是已经调用过 `uvmunmap` 释放了所有物理内存页并清除了第三级页表的 PTE。
2. `freewalk` 作用：主要是释放了页表。*清零...感觉不是很必要*

递归调用：只要不是叶子页表，那么就继续递归，直到是叶子页表为止。

大致流程：

- 最外层的 `freewalk` 的参数就是第一级页表，直接遍历其 512 项，也就是第二级页表，如果可以访问那么就开始递归调用 `freewalk`，并进行清零，最后释放该页表。
- 第二级页表的 `freewalk` 遍历第三级页表，同样递归调用、清零以及释放页表。
- 第三级页表的 `freewalk` 遍历的就是它持有的物理地址，由于之前所有的物理内存页都被释放且 PTE 被清空，那么至少所有的 PTE 的 Valid 位都为 0——如果有不为 0 的，那么就 panic。当然最后需要释放自己。

## 7 vmprint 实现

模仿 `freewalk` 的方式，我们来写 vmprint，也不多废话了直接上代码。

递归的主体函数是 `_vmprint`。本来按照递归的写法是不需要添加页表级数的参数 level 的，但是为了打点不得不加。

*注意不要和 `walk` 里的 level 搞混了。*

和 `freewalk` 的区别是，至少这次三级页表的 PTE 上有点东西可以访问了。递归同样在这里停止。

```C
void printDot(int level)
{
  if (level == 1)
    printf("..");
  else if (level == 2)
    printf(".. ..");
  else
    printf(".. .. ..");
}

void _vmprint(pagetable_t pagetable, int level)
{
  for (int i = 0; i < 512; i++)
  {
    pte_t pte = pagetable[i];
    if ((pte & PTE_V) && (pte & (PTE_R | PTE_W | PTE_X)) == 0)
    {
      uint64 child = PTE2PA(pte);
      printDot(level);
      printf("%d: pte %p pa %p\n", i, pte, child);
      _vmprint((pagetable_t)child, level + 1);
    }
    else if (pte & PTE_V)
    {
      uint64 child = PTE2PA(pte);
      printDot(level);
      printf("%d: pte %p pa %p\n", i, pte, child);
    }
  }
}

void vmprint(pagetable_t pagetable)
{
  printf("page table %p\n", pagetable);
  _vmprint(pagetable, 1);
}
```
