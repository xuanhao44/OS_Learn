# MIT 6.S081 - chapter 3 note 1

大致对应 3.1 Paging Hardware

从逻辑地址到物理地址的翻译。

*本篇仅介绍页表核心逻辑，不介绍其他内存管理知识。*

## 1 一些理论数字

- RISC-V 的寄存器位数：64 位。

- 物理内存地址：56 位。
  - 选择 56bit 而不是 64bit 是因为在主板上只需要 56 根线。
- 逻辑内存地址：39 位，约为 512 GB。
  - 未使用的 25 bit：可拓展，作为逻辑地址的一部分。

## 2 逻辑内存、物理内存地址对应

- 考虑这样一个双射表单——地址表：以地址为粒度来管理。
  - 表单中有 $2^{39}$ 项，非常巨大。
  - 考虑到表单是在内存中的，这样内存会被表单耗尽。
- 所以一定不是为每个地址创建一条表单条目，而是为每个 page 创建一条表单条目。
- **以 page（4096 bytes，4 KB）为粒度管理——页表**。

## 3 逻辑内存、物理内存地址拆分

- 映射关系：硬件（MMU 等等）、软件都有涉及实现。
- 拆分 39 bit 逻辑内存地址：
  - 27 bit 被用来当做 index，12 bit 被用来当做 offset。
- 拆分 56 bit 物理内存地址：
  - 44 bit 是物理 page 号（PPN，**Physical Page Number**）
  - 剩下 12 bit 是 offset 完全继承自逻辑内存地址

## 4 最简单的页表，以及翻译方式

<img src="https://typora-1304621073.cos.ap-guangzhou.myqcloud.com/typora/%E5%8F%AA%E6%9C%89%E4%B8%80%E7%BA%A7%E9%A1%B5%E8%A1%A8.png" alt="只有一级页表" style="zoom: 33%;" />

- **页表的构成：（44 bit）PPN + （10 bit）Flags（暂时不解释 Flags）= 54 bit**
- index 用来查找 page，offset 对应的是一个 page 中的哪个字节。
- 读取虚拟内存地址中的 index 可以知道物理内存中的 page 号，这个 page 号对应了物理内存中的 4096 个字节。
- 之后虚拟内存地址中的 offset 指向了 page 中的 4096 个字节中的某一个。

## 5 页表是多级结构

index 有 27 位，而一个 index 对应一个 page，那么我们的这个页表就最多有 $2^{27}$ 项。这和上面提到的 $2^{39}$ 一样，对于内存来说是个较大的数字。

所以页表不是这样存储的，实际上是一个**多级的结构**。

<img src="https://typora-1304621073.cos.ap-guangzhou.myqcloud.com/typora/%E4%B8%89%E7%BA%A7%E9%A1%B5%E8%A1%A8.png" alt="三级页表" style="zoom: 33%;" />

我们之前提到的虚拟内存地址中的 27bit 的 index，实际上是由 3 个 9 bit 的数字组成：（L2，L1，L0）。9 bit 用来索引一个表单，那么一个表单上就只有 512 个项。一个表单的结构和之前的页表是一样的。

*page table，page directory， page directory table 区分并不明显，可以都认为是有相同结构的地址对应表单。*

SATP 寄存器会指向最高一级的 page directory 的物理内存地址。

前 9 个 bit，即 L2，被用来索引最高级的 page directory。那么这个 page directory 的 PPN 又指向中间级的 page directory。

当我们在使用中间级的 page directory 时，我们使用 L1 作为索引找 PPN，那么这个 PPN 又指向最低级的 page directory。

当我们在使用最低级的 page directory 时，我们使用 L0 作为索引找 PPN，那么这个 PPN 就能对应到物理内存地址了。

在前一个方案中，虽然我们只使用了一个 page，还是需要 $2^{27}$ 个PTE。这个方案中，我们只需要 $3 * 2^9$ 个 PTE。

## 6 计算 page table 的物理地址

“SATP 寄存器指向最高级 page directory” 具体是如何操作的？

SATP寄存器会指向最高一级的 page directory 的物理内存地址。这是直接的。

那 “PPN 指向下一级 page directory” 具体是如何操作的？

PPN 并不是物理内存地址。所以我们用 44 bit 的 PPN，**再加上 12bit 的 0**，这样就得到了下一级page directory 的 56 bit 物理地址。这里要求每个 page directory 都与物理 page 对齐（大概是通过对齐解决问题，但是还不是很明白）。

## 7 Flags 和页表结构拓展

所有的 page directory 传递的都是 PPN，对应的物理地址是 44 bit 的 PPN 加上 12 bit 的 Flags。

![页表中的Flag](https://typora-1304621073.cos.ap-guangzhou.myqcloud.com/typora/%E9%A1%B5%E8%A1%A8%E4%B8%AD%E7%9A%84Flag.png)

支持 page 的硬件在低 10 bit 存了一些标志位用来控制地址权限。

- 第一个标志位是 Valid。如果 Valid bit 位为 1，那么表明这是一条合法的 PTE，你可以用它来做地址翻译。被使用的 PTE 的 Valid bit 位会被设置成 1，其他的还没有被使用的 PTE 的 Valid bit 为 0。这个标志位告诉 MMU，你不能使用这条 PTE，因为这条 PTE 并不包含有用的信息。
- 下两个标志位分别是 Readable 和 Writable。表明你是否可以读/写这个 page。
- Executable 表明你可以从这个 page 执行指令。
- User 表明这个 page 可以被运行在用户空间的进程访问。
- 其他标志位并不是那么重要，他们偶尔会出现，前面 5 个是重要的标志位。