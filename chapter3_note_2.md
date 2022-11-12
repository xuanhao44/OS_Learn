# MIT 6.S081 - chapter 3 note 2

## 0 起始

### 0.1 涉及章节以及参考

本文大致对应：

- 3.2 Kernel address space
- 3.3 Code: Creating an Address Space
- 3.6 Process address space
- 3.7 Code: sbrk
- 3.8 Code: exec

3.4、3.5、3.9 不会细讲。

参考：

- xv6 book chapter 2、3
- [Chapter 3: Page Tables - 知乎 (zhihu.com)](https://zhuanlan.zhihu.com/p/351646541)
- [MIT 6.S081 Lecture Notes | Xiao Fan (樊潇) (fanxiao.tech)](https://fanxiao.tech/posts/MIT-6S081-notes/#lecture-3-page-tables)
- [Lec04 Page tables (Frans) - MIT6.S081 (gitbook.io)](https://mit-public-courses-cn-translatio.gitbook.io/mit6-s081/lec04-page-tables-frans)

### 0.2 简略的复习 xv6 启动过程

更具体的流程在：*2.6 Code: Starting Xv6 and the First Process*

1. qemu 从地址 0x80000000 开始加载内核(`kernel.ld`)，也即设置了 kernel text 和 kernel data。
2. xv6 从 `entry.s` 开始启动(in machine mode)，最后 call start。
3. 到 `start.c` 的 start()，进行设置，最后切换到 supervisor mode，然后 jump to main()。
4. 然后就是在 supervisor mode 下熟悉的 main 函数了，进行一系列有一定顺序关系的初始化。
5. 当其他大部分初始化设置结束后，看向 userinit；总的来说，它的作用是初始化第一个用户程序 initcode。然后该程序被调度器调度运行。
6. 二进制小程序 initcode (对应汇编代码：`user/initcode.S`)被运行，内容简单来说就是调用了系统调用 exec 去运行 `user/init`，exec 会替换当前进程并执行目标程序，也就是说，init 程序就在 initcode 上生长出来了。
7. init 程序：配置好 console，调用 fork，并在 fork 出的子进程中执行 shell。

整个流程大致如此，我们提到启动过程的原因是我们将要说的内容，无论是内核地址空间，还是用户地址空间，又或是 sbrk 和 exec 调用，都可以被这个启动的过程串起来。不要在学一部分内容时忘记了这部分在整体中的作用。

下面也不会按照 xv6 book 中的顺序去讲，也是按照启动的过程去理解。我个人认为这是知识的再梳理而不是初学，所以请先看 xv6 book。

### 0.3 地址空间图示

在 xv6 book 中，3.2 Kernel address space 和 3.6 Process address space 都是在比较开头的时候讲的——我很确信这一部分的内容在你第一遍看的时候理解不会很深，只能说看了一眼大概。

不过还是要先看一眼，至少有个印象。接下来你会见到这两张图中的地址空间是如何被一步步设置好的，所以会见到它们很多次。

*图是从 2022 版的 xv6 book 中截取的。因为 2020 版画的比较乱。*

内核地址空间

<img src="https://typora-1304621073.cos.ap-guangzhou.myqcloud.com/typora/kernel_address_space.png" alt="kernel_address_space" style="zoom:50%;" />

用户地址空间

<img src="https://typora-1304621073.cos.ap-guangzhou.myqcloud.com/typora/user_address_space.png" alt="user_address_space" style="zoom:50%;" />

## 1 kinit、kvminit、kvminithart、procinit

我们先来看 main 函数中的这四个被调用的函数。总的来说，它们完成了内存的初始化，以及内核地址空间的初始化。

```c
kinit();         // physical page allocator
kvminit();       // create kernel page table
kvminithart();   // turn on paging
procinit();      // process table
```

### 1.1 kinit & kalloc

*3.4 Physical Memory Allocation & 3.5 Code: Physical Memory Allocator*

*比较神奇的是，我先做了 lab8，所以 free memory 的管理是比较清楚的。kalloc.c 起到什么作用我就不提了。*

我们首先需要组织可用的内存，**kinit 做的正是组织而非使用或者建立映射的工作**。

从 kernel data 结束开始，到 PHYSTOP 为止，这一部分称为 free memory，用于运行时的内存分配。kernel data 结束的位置是 end，在 `kernel.ld` 中。所以在组织 free memory 的时候是这样写的：

```c
void
kinit()
{
  initlock(&kmem.lock, "kmem");
  freerange(end, (void*)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
    kfree(p);
}
```

打 log 输出一下 end 和 PGROUNDUP((uint64)pa_start)。

```c
end = 0x0000000080027020
PGROUNDUP((uint64)pa_start) = 0x0000000080028000
```

PGROUNDUP 的作用就是把一个不是 PGSIZE 的整数倍的地址，向上抬到更高的 PGSIZE 的整数倍的地址。

```c
#define PGROUNDUP(sz)  (((sz)+PGSIZE-1) & ~(PGSIZE-1))
#define PGROUNDDOWN(a) (((a)) & ~(PGSIZE-1))
```

站在后面内容的角度来看，end 虽然是 kernel data 结束的位置，但是我们设置 free memory 时，这一段不完整的内存我们就不要了，从 PGROUNDUP((uint64)pa_start) 开始就行。

<img src="https://typora-1304621073.cos.ap-guangzhou.myqcloud.com/typora/kernel_address_space_kinit.jpg" alt="kernel_address_space_kinit" style="zoom:50%;" />

（讲解略）kalloc 在分配物理帧时，**从大的物理地址开始往下分配**。还要注意 kalloc 返回的是物理地址。

### 1.2 kvminit

从这里开始了解内核地址空间的设置。

*3.3 Code: Creating an Address Space*

作用：创建内核页表，建立一系列的直接映射。

注意：页表只是被创建，然而并没有被装载到 SATP 寄存器上。

创建内核页表：*考虑到 kalloc 从大地址往下分配，而 kvminit 几乎是最早调用 kalloc 的，可以想象 kernel_pagetable 处于 free memory 的很高的位置。*

```c
kernel_pagetable = (pagetable_t)kalloc();
memset(kernel_pagetable, 0, PGSIZE);
```

*之后用户页表的创建和这个基本没有差别，见 uvmcreate()*

建立一系列直接映射：UART0、VIRTIO0、CLINT、PLIC、KERNBASE、etext。

使用 kvmmap，如：

```c
// CLINT
kvmmap(CLINT, CLINT, 0x10000, PTE_R | PTE_W);
```

结合下文，至少可以发现 va 和 pa 都是一样的。这便是直接映射。

*这些地址都在 `kernel/memlayout.h` 中定义，有的是内核地址空间专属，有的则是内核和用户地址空间都可以使用的。*

kvmmap 基本没干什么事情，就是调用 mappages 这个通用的建立页表映射项的函数。你可以认为，kvmmap 就是单纯的为内核页表提供建立映射的函数。

```c
// add a mapping to the kernel page table.
// only used when booting.
// does not flush TLB or enable paging.
void kvmmap(uint64 va, uint64 pa, uint64 sz, int perm)
{
  if (mappages(kernel_pagetable, va, sz, pa, perm) != 0)
    panic("kvmmap");
}
```

mappages 和 walk 函数略。

最后注意：**TRAMPOLINE 是一个异类。它并不是直接映射。**

```c
// map the trampoline for trap entry/exit to
// the highest virtual address in the kernel.
kvmmap(TRAMPOLINE, (uint64)trampoline, PGSIZE, PTE_R | PTE_X);
```

TRAMPOLINE 被设置在了虚拟地址空间最高的地方。

```c
// map the trampoline page to the highest address,
// in both user and kernel space.
#define TRAMPOLINE (MAXVA - PGSIZE)
```

MAXVA = (1L << (9 + 9 + 9 + 12 - 1)) ，写成 16 进制，那么就是 1 << 38 = 0x40 0000 0000。很明显如果真的是直接映射到物理地址的话，那需要的物理地址就太多了。

看图可知，TRAMPOLINE 映射到 kernel text 的位置。`(uint64)trampoline` 具体是个啥要去问 `kernel.ld`，咱也没看太懂。

<img src="https://typora-1304621073.cos.ap-guangzhou.myqcloud.com/typora/kernel_address_space_kvminit.jpg" alt="kernel_address_space_kvminit" style="zoom:50%;" />

### 1.3 kvminithart

*still 3.3 Code: Creating an Address Space*

作用：装载内核页表，刷新页表缓存。

一个用于设置 SATP 寄存器，一个刷新 TLB 缓存。这个函数不止在 main 函数中被使用，比如接下来的 procinit 还会调用它一次。

### 1.4 procinit

*still 3.3 Code: Creating an Address Space*

**为每个用户进程分配一个内核栈，该内核栈将被映射到内核虚拟地址空间的高地址部分，位于 trampoline 下方。**生成虚拟地址的步长为 2 页，而且只处理低的那一页，这样高的一页就自动成了保护页（PTE_V 无效）。

```C
char *pa = kalloc();
if(pa == 0)
  panic("kalloc");
uint64 va = KSTACK((int) (p - proc));
```

这些内核栈的物理地址是从 kalloc 得来的，也就是说不是直接映射。

查看这些栈的虚拟地址的算法：使用的参数是 `(int) (p - proc)`——当前进程 p 和 proc 数组最开始位置的偏移量。

```c
// map kernel stacks beneath the trampoline,
// each surrounded by invalid guard pages.
#define KSTACK(p) (TRAMPOLINE - ((p)+1)* 2*PGSIZE)
```

比如第一次从 p - proc = 0 开始，va = TRAMPOLINE - 2*PGSIZE。即 TRAMPOLINE 的下面第一页是 guard page，第二页是 kstack，也就是 va 指向的位置。

```c
kvmmap(va, (uint64)pa, PGSIZE, PTE_R | PTE_W);
```

内核栈可读可写，但在用户态不可访问，也不能直接执行。

<img src="https://typora-1304621073.cos.ap-guangzhou.myqcloud.com/typora/kernel_address_space_procinit.jpg" alt="kernel_address_space_procinit" style="zoom:50%;" />

更新了所有内核栈的 PTE 之后，最后调用 kvminithart 更新一次 SATP 寄存器，分页硬件就能使用新的页表。

那么到这里，内核地址空间的设置就结束了。*(kernel text 和 kernel data 在最开始就被 qemu 载入了)*

## 2 userinit

*2.6 Code: Starting Xv6 and the First Process*

作用：初始化第一个用户程序 initcode。

一段代码被装载入该进程的虚拟地址空间，并将该进程的状态设置为 RUNNABLE 之后，该进程就能被调度器发现，然后被调度并执行，因此这就是我们运行的**第一个用户进程**。

我们从这里开始了解用户地址空间的设置。

```c
// Set up first user process.
void
userinit(void)
{
  struct proc *p;

  p = allocproc();
  initproc = p;
  
  // allocate one user page and copy init's instructions
  // and data into it.
  uvminit(p->pagetable, initcode, sizeof(initcode));
  p->sz = PGSIZE;

  // prepare for the very first "return" from kernel to user.
  p->trapframe->epc = 0;      // user program counter
  p->trapframe->sp = PGSIZE;  // user stack pointer

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");

  p->state = RUNNABLE;

  release(&p->lock);
}
```

在内存管理方面，我们需要关注的是 allocproc 和 uvminit 这两个函数。

### 2.1 allocproc - proc_pagetable

allocproc 是选取了一个 UNUSED proc，然后设置好相关参数后返回这个进程，也就是拿来用。

我们关心的部分是其中的两个部分：

```c
  // Allocate a trapframe page.
  if((p->trapframe = (struct trapframe *)kalloc()) == 0){
    release(&p->lock);
    return 0;
  }

  // An empty user page table.
  p->pagetable = proc_pagetable(p);
  if(p->pagetable == 0){
    freeproc(p);
    release(&p->lock);
    return 0;
  }
```

我们先为 trapframe 分配了一页的内存，但是并为其设置映射关系——毕竟此时页表还不存在。

 我们再来看 proc_pagetable 函数。主要顺序完成了这些事情：

1. 创建了空页表。
2. 只在该页表上添加了 trampoline 和 trapframe 的映射，**其它的虚拟地址空间都暂时为空**。

注意到在设置 trampoline 方面，内核地址空间和用户地址空间有相同的逻辑地址，也有相同的物理地址，这意味着一个物理地址被映射到了多个虚拟地址中；用户地址空间还有 trapframe，被设置在 trampoline 的下面。

```c
// Create a user page table for a given process,
// with no user memory, but with trampoline pages.
pagetable_t
proc_pagetable(struct proc *p)
{
  pagetable_t pagetable;

  // An empty page table.
  pagetable = uvmcreate();
  if(pagetable == 0)
    return 0;

  // map the trampoline code (for system call return)
  // at the highest user virtual address.
  // only the supervisor uses it, on the way
  // to/from user space, so not PTE_U.
  if(mappages(pagetable, TRAMPOLINE, PGSIZE,
              (uint64)trampoline, PTE_R | PTE_X) < 0){
    uvmfree(pagetable, 0);
    return 0;
  }

  // map the trapframe just below TRAMPOLINE, for trampoline.S.
  if(mappages(pagetable, TRAPFRAME, PGSIZE,
              (uint64)(p->trapframe), PTE_R | PTE_W) < 0){
    uvmunmap(pagetable, TRAMPOLINE, 1, 0);
    uvmfree(pagetable, 0);
    return 0;
  }

  return pagetable;
}
```

![user_address_space_allocproc](https://typora-1304621073.cos.ap-guangzhou.myqcloud.com/typora/user_address_space_allocproc.jpg)

### 2.2 uvminit

```c
// allocate one user page and copy init's instructions
// and data into it.
uvminit(p->pagetable, initcode, sizeof(initcode));
```

到之前为止，这个用户进程被创建了，设置了页表。很明显，用户地址空间中还要放程序本身。uvminit 就是做这样一件事：

- 用 kalloc 申请一页内存；
- 建立映射——逻辑地址是 0，物理地址是 kalloc 的返回值 mem，即申请到的物理内存地址；
- 把 initcode 放到用 kalloc 中申请这一页中。

```c
// Load the user initcode into address 0 of pagetable,
// for the very first process.
// sz must be less than a page.
void uvminit(pagetable_t pagetable, uchar *src, uint sz)
{
  char *mem;

  if (sz >= PGSIZE)
    panic("inituvm: more than a page");
  mem = kalloc();
  memset(mem, 0, PGSIZE);
  mappages(pagetable, 0, PGSIZE, (uint64)mem, PTE_W | PTE_R | PTE_X | PTE_U);
  memmove(mem, src, sz);
}
```

![user_address_space_uvminit](https://typora-1304621073.cos.ap-guangzhou.myqcloud.com/typora/user_address_space_uvminit.jpg)

所以到现在，initcode 就给人一种“麻雀虽小，五脏俱全”的感觉。只不过，这个用户进程地址空间还没有之前看到的 data，guard page，stack 等。

userinit 就讲到这里，接下来就是被调度器调度运行。运行，也就是 exec("/init")。我们接下来就来讲 exec 系统调用。

系统调用的流程属于 chapter 2 内容，就省略了。

## 3 sbrk

*3.6 Process Address Space & 3.7 Code: Sbrk*

*sbrk 和启动以及第一个进程没有直接联系，主要是为了先讲 uvmmalloc 和 uvmdealloc 这两个函数。*

应用程序使用 sbrk 系统调用向内核请求**堆内存**。堆在栈的上方，并且堆的用户地址空间是随着 p->sz 一直向上的。

![user_address_space_sbrk](https://typora-1304621073.cos.ap-guangzhou.myqcloud.com/typora/user_address_space_sbrk.jpg)

**Sbrk** 是一个系统调用，用户进程调用它以增加或减少自己拥有的物理内存（proc->sz）。**growproc** 根据增加或减少内存的需要，又分别调用 uvmmalloc 和 uvmdealloc 来满足请求。

- **uvmalloc **通过调用 kalloc 来分配物理内存，并调用 mappages 来更新页表，并设置 PTE 的 5 个标志位都置位。
- **uvmdealloc** 调用 uvmunmap 来回收已分配的物理内存。
- **uvmunmap** 使用 walk 找到相应的 PTE，并且调用 kfree 回收相应的物理帧。

## 4 exec

*3.6 Process Address Space & 3.8 Code: Exec*

我们之前知道，系统调用 exec 将存储在文件系统上的，新的用户程序装载进内存里，然后执行它。现在我们将进一步观察，在 exec 中，这个新用户进程的虚拟地址空间是怎么被建立起来的。

我们知道在 exec 之前，PCB 中是有一定内容的，也就是说有页表，还有一些其他的数据。就算是最开始的 initcode 进程，也不是完全空白的。但是 exec 不仅不需要这些数据，甚至最后把这些内存以及页表全部丢弃了，大部分的 PCB 也都是新设置的。

*请注意，在下面的介绍中有选择的省略掉了有关文件系统的部分。*

### 4.1 初始化页表

exec 为用户进程调用 proc_pagetable，通过 uvmcreate 创建一个空的用户页表，接着只在该页表上添加了 trampoline 和 trapframe 的映射，其它的虚拟地址空间都暂时为空。

```c
if ((pagetable = proc_pagetable(p)) == 0)
    goto bad;
```

### 4.2 加载 ELF 程序到内存中

然后，exec 对于每个程序段，先是调用 uvmalloc 分配足够的物理帧，显然用户页表也更新了。然后调用 loadseg 加载程序段到这些物理帧中。

*loadseg 讲解略。*

```c
  // Load program into memory.
  for (i = 0, off = elf.phoff; i < elf.phnum; i++, off += sizeof(ph))
  {
    if (readi(ip, 0, (uint64)&ph, off, sizeof(ph)) != sizeof(ph))
      goto bad;
    if (ph.type != ELF_PROG_LOAD)
      continue;
    if (ph.memsz < ph.filesz)
      goto bad;
    if (ph.vaddr + ph.memsz < ph.vaddr)
      goto bad;
    uint64 sz1;
    if ((sz1 = uvmalloc(pagetable, sz, ph.vaddr + ph.memsz)) == 0)
      goto bad;
    sz = sz1;
    if (ph.vaddr % PGSIZE != 0)
      goto bad;
    if (loadseg(pagetable, ph.vaddr, ip, ph.off, ph.filesz) < 0)
      goto bad;
  }
```

![user_address_space_loadelf](https://typora-1304621073.cos.ap-guangzhou.myqcloud.com/typora/user_address_space_loadelf.jpg)

至此，exec 已经将用户程序的各程序段都装载完成了。

### 4.3 分配、初始化栈

exec 首先分配**两页物理帧**。

```c
sz = PGROUNDUP(sz);
uint64 sz1;
if ((sz1 = uvmalloc(pagetable, sz, sz + 2 * PGSIZE)) == 0)
   goto bad;
sz = sz1;
```

第一页用作保护页，通过调用 uvmclear 将 PTE_U 设为无效，这样在用户空间下不能访问它。

```c
uvmclear(pagetable, sz - 2 * PGSIZE);
```

第二页留给用户栈。

```c
sp = sz;
stackbase = sp - PGSIZE;
```

sp 的位置是 sz，也就是现在用户自下而上的 memory 最高的位置；而 stackbase 被设置在 sp -PGSIZE。

故栈的大小是一页 4096 bytes，且栈的地址使用是从上到下，即栈顶为 sp，然后不超过 stackbase。

![user_address_space_stack](https://typora-1304621073.cos.ap-guangzhou.myqcloud.com/typora/user_address_space_stack.jpg)

从栈顶开始，把一些东西推入用户栈内：

- 命令行参数的字符串

```c
  // Push argument strings, prepare rest of stack in ustack.
  for (argc = 0; argv[argc]; argc++)
  {
    if (argc >= MAXARG)
      goto bad;
    sp -= strlen(argv[argc]) + 1;
    sp -= sp % 16; // riscv sp must be 16-byte aligned
    if (sp < stackbase)
      goto bad;
    if (copyout(pagetable, sp, argv[argc], strlen(argv[argc]) + 1) < 0)
      goto bad;
    ustack[argc] = sp;
  }
  ustack[argc] = 0;
```

- 指向这些命令行参数的指针数组 argv[ ]

```c
  // push the array of argv[] pointers.
  sp -= (argc + 1) * sizeof(uint64);
  sp -= sp % 16;
  if (sp < stackbase)
    goto bad;
  if (copyout(pagetable, sp, (char *)ustack, (argc + 1) * sizeof(uint64)) < 0)
    goto bad;
```

- 用于从调用 main(argc, argv[ ]) 返回的其它参数（argc、argv 指针和伪造的返回 pc 值）

```c
  // arguments to user main(argc, argv)
  // argc is returned via the system call return
  // value, which goes in a0.
  p->trapframe->a1 = sp;

  // Save program name for debugging.
  for (last = s = path; *s; s++)
    if (*s == '/')
      last = s + 1;
  safestrcpy(p->name, last, sizeof(p->name));
```

最后，当用户程序的程序段都成功加载，用户栈也设置完毕后，内核确定这次 exec 将要成功时，**exec 就清除进程的旧内存映像，即释放旧页表所占用的物理内存，并准备使用新的页表**。然后系统调用 exec 将会顺利完成并返回，该进程将执行一个新的用户程序。

```c
  // Commit to the user image.
  oldpagetable = p->pagetable;
  p->pagetable = pagetable;
  p->sz = sz;
  p->trapframe->epc = elf.entry; // initial program counter = main
  p->trapframe->sp = sp;         // initial stack pointer
  proc_freepagetable(oldpagetable, oldsz);

  if (p->pid == 1)
    vmprint(p->pagetable);

  return argc; // this ends up in a0, the first argument to main(argc, argv)
```

