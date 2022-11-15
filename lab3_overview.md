# MIT 6.S081 - Lab Page tables - overview

在做实验之前，阅读 [xv6手册](https://pdos.csail.mit.edu/6.828/2020/xv6/book-riscv-rev1.pdf) 的以下章节及相关源代码：

- [1] xv6 book, Chapter 3 Page tables (页表)
- [2] kernel/memlayout.h （定义内存的布局）
- [3] kernel/vm.c （虚拟内存代码）
- [4] kernel/kalloc.c （分配和释放物理内存代码）

## 起始

王道考研关于内存管理这一块的总体的知识比 MIT 课程全面一些，但是在细节上不如 MIT 课程；

<img src="https://typora-1304621073.cos.ap-guangzhou.myqcloud.com/typora/%E5%86%85%E5%AD%98%E7%AE%A1%E7%90%86%E7%9F%A5%E8%AF%86%E6%A1%86%E6%9E%B6.png" alt="内存管理知识框架" style="zoom: 33%;" />

在硬件方面 MIT 或者说 xv6book 的讲解不多，可以到 [riscv-privileged](https://riscv.org/technical/specifications/) 中去找相应的部分去看。

<img src="https://typora-1304621073.cos.ap-guangzhou.myqcloud.com/typora/riscv-privileged.png" alt="riscv-privileged" style="zoom: 33%;" />

## 结束：报告

### 一 回答问题

#### 1 查阅资料，简要阐述页表机制为什么会被发明，它有什么好处？

- 页表机制为什么会被发明？
  - 页映射是虚拟存储机制的一部分，它随着虚拟存储的发明而诞生。
- 有什么好处？*那当然是为了发展虚拟内存啦！？*
  - 分页存储管理方式是非连续分配管理方式的一种，另一种是分段存储管理方式。
  - 虚拟内存技术的一个前提是允许一个作业分多次调入内存，其实现建立在离散分配的内存管理方式的基础上。（*请求分页存储管理）*

#### 2 按照步骤，阐述 SV39 标准下，给定一个 64 位虚拟地址为 0x123456789ABCDEF 的时候，是如何一步一步得到最终的物理地址的？（页表内容可以自行假设）

在 SV39 标准下，逻辑地址为 0x6789ABCDEF（39 bit）。

110 0111 1000 1001 1010 1011 1100 1101 1110 1111

拆分为：

- L2：110 0111 10，即 414；
- L1：00 1001 101，即 77；
- L0：0 1011 1100，即 188；
- offset：1101 1110 1111，即 3567；

从 SATP 寄存器得来第一级页表的起始物理地址，根据 L2，也就是 414，在第一级页表上找到对应序号的 PPN，记为 PPN_2；PPN_2 长度 44 位，是第二级页表的起始物理地址的前 44 位。第二级页表的起始物理地址应为 PPN_2 后面跟上 12 bit 的 0。

根据 L1，也就是 77，在第二级页表上找到对应序号的 PPN，记为 PPN_3；PPN_3 长度 44 位，是第三级页表的起始物理地址的前 44 位。第三级页表的起始物理地址应为 PPN_3 后面跟上 12 bit 的 0。

根据 L0，也就是 188，在第三级页表上找到对应序号的 PPN，记为 PPN_END；PPN_END 长度 44 位，是逻辑地址对应的真实物理地址的前 44 位。故逻辑地址对应的真实物理地址应为 PPN_END 后面跟上 12bit 的 offset。这样就获得了最终的物理地址。

#### 3 我们注意到，虚拟地址的 L2, L1, L0 均为 9 位。这实际上是设计中的必然结果，它们只能是 9 位，不能是 10 位或者是 8 位，你能说出其中的理由吗？（提示：一个页目录的大小必须与页的大小等大）

一个页目录的大小必须与页的大小等大，而一页的大小一般为 4 KB，也就是 4096 bytes。

那么在 RV64 中，不管 PTE 多少位，一项就是 64 bits，也就是 8 bytes。那么一个页表中就只能有 $4096 / 8 = 512$ 项。也就是说，只要是 RV64，不管你是 SV39，还是 SV48，一个页表中就只能有 512 项。

各级页表的结构是相同的，那么对应的 index 的位数也应该是相同的，都对应 9 位。

利用同样的道理去分析 RV32 下的虚拟地址，就更能理解了：

页的大小仍然是 4 KB，但是 PTE 一项只有 32 bits，也就是 4 bytes，那么一个页表中就有 $4096 / 4 = 1024$ 项。

一般使用 SV32，偏移量 offset 仍为 12 位，那么 index 就有 20 位，共两级页表，各对应 10 位。

*至于 PTE 的结构，也是 32 bits，有 10 bits 的 Flags，22 bits 的 PPN。*

#### 4 在“实验原理”部分，我们知道 SV39 中的 39 是什么意思。但是其实还有一种标准，叫做 SV48，采用了四级页表而非三级页表，你能模仿“实验原理”部分示意图，画出 SV48 页表的数据结构和翻译的模式图示吗？（SV39 原图请参考指导书）

SV48：48 bit 的逻辑地址，除去 12 bit 的 offset，那么 index 就有 36 bit，考虑到采用了四级页表，那么每一级页表就对应 9 bit 的 index，也就是有 512 项。

<img src="https://typora-1304621073.cos.ap-guangzhou.myqcloud.com/typora/SV48-%E5%9B%9B%E7%BA%A7%E9%A1%B5%E8%A1%A8.jpg" alt="SV48-四级页表" style="zoom: 33%;" />

### 二 实验详细设计

（包括学习过程）

- [MIT 6.S081 - chapter 3 note 1](https://www.sheniao.top/os/139.html)
- [MIT 6.S081 - Lab Page tables - vmprint](https://www.sheniao.top/os/138.html)
- [MIT 6.S081 - chapter 3 note 2](https://www.sheniao.top/os/140.html)
- [MIT 6.S081 - Lab Page tables - A kernel page table per process](https://www.sheniao.top/os/142.html)
- [MIT 6.S081 - Lab Page tables - Simplify copyin/copyinstr](https://www.sheniao.top/os/163.html)

### 三 实验结果截图

<img src="https://typora-1304621073.cos.ap-guangzhou.myqcloud.com/typora/lab3_grade.png" alt="lab3_grade" style="zoom:67%;" />

