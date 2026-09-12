# Lesson 12 / Lab 3：TinyReassembler

> 预计时间：2–3.5 小时
>
> 前置阅读：[[../../12_乱序子串重组|第 12 课：乱序子串重组器]]、[[../Lab2_TinyStream/README|Lab 2：TinyStream]]
>
> 你只需修改：`src/tiny_reassembler.cc`。不要求实验报告；通过测试并保留有意义的代码注释即可。

## 这次要造什么

上一课已经把接收路径拆成：

```text
带位置的网络片段 → Reassembler → ByteStream → Reader
```

现在实现中间的 Reassembler。输入是：

```text
(first_index, data, is_last_substring)
```

输出不是屏幕打印，也不是网络发送；它指重组器按顺序 `push` 进接收端 ByteStream 的字节。

本 Lab 故意使用：

```cpp
std::map<std::uint64_t, char> pending_;
```

每个逻辑位置最多保存一个字符。它不是最高效的真实 TCP 实现，但能把窗口、去重和连续输出直接写成可观察状态。先做对，再在以后讨论区间合并优化。

## 目录与阅读顺序

```text
Lab3_TinyReassembler/
├── CMakeLists.txt
├── README.md
├── src/
│   ├── tiny_stream.hh
│   ├── tiny_stream.cc          # Lab 2 已完成版本，不修改
│   ├── tiny_reassembler.hh     # 状态和公共契约，先读
│   └── tiny_reassembler.cc     # 唯一需要实现的文件
└── tests/
    └── lab3_tests.cc           # 八组行为测试
```

先阅读头文件中的 XML 注释，再看测试函数名称，最后按四个阶段实现。`output()`、`first_unassembled()` 等观察函数已经完成，不需要重写。

## 编译与第一次运行

在 Developer PowerShell for Visual Studio 中运行：

```powershell
cd "C:\Users\Lenovo\Desktop\learn\计算机网络\Labs\Lab3_TinyReassembler"; cmake -S . -B build; cmake --build build --config Debug; ctest --test-dir build -C Debug --output-on-failure
```

在 WSL 中运行：

```bash
cd "/mnt/c/Users/Lenovo/Desktop/learn/计算机网络/Labs/Lab3_TinyReassembler" && cmake -S . -B build && cmake --build build -j && ctest --test-dir build --output-on-failure
```

起始代码应该编译成功，但测试失败。因为 `insert` 和两个私有动作仍是空实现，这不是环境错误。

## 必须一直成立的三个不变量

```text
first_unassembled == output().bytes_pushed()
bytes_pending() == pending_ 中不同逻辑位置的数量
output().bytes_buffered() + bytes_pending() <= capacity
```

第三个不变量不要求你额外保存 `capacity_`。当前可接受窗口右端可以从 ByteStream 得到：

```text
first_unacceptable = first_unassembled + output_.available_capacity()
```

`available_capacity()` 已经扣除了仍在 ByteStream 中、尚未被 Reader `pop` 的字节。

## 阶段 1：先记录 EOF，暂时不关闭

若原始输入满足：

```text
first_index = 5, data.size() = 5, is_last_substring = true
```

则：

```text
eof_index = 10
```

必须在裁剪片段前，用**原始片段末端**计算 EOF。最后片段可能完全位于窗口外，但它仍然告诉了你流在哪里结束。

此阶段只记录位置，不能因为看到 `true` 就立即关闭 Writer。

## 阶段 2：裁剪并去重

设：

```text
left = first_unassembled()
right = left + output_.available_capacity()
```

对输入中的每个字符，根据它的绝对逻辑位置判断：

```text
position < left   → 已经交付，忽略
position >= right → 当前窗口外，忽略
left <= position < right → 尝试保存
```

`std::map::try_emplace(position, character)` 只在 key 不存在时插入，很适合去重：

```cpp
pending_.try_emplace(position, data[offset]);
```

不要把 `data.size()` 直接加到待重组计数；重复位置不会增加 `pending_.size()`。

### 一个整数边界提醒

不要先算 `first_index + data.size()` 再遍历到它，因为极大输入可能发生无符号整数溢出。这个 Lab 的简单写法可以遍历 `offset`，并在计算 `first_index + offset` 前检查：

```cpp
if (offset > UINT64_MAX - first_index) break;
```

EOF 末端也需要同类保护。测试主要检查网络机制，这条提示用来养成边界意识。

## 阶段 3：只输出连续前缀

从：

```text
position = first_unassembled()
```

开始，在 `pending_` 中逐个查找：

```cpp
const auto found = pending_.find(position);
```

遇到第一个不存在的位置就停止。可以把连续字符先收集进一个临时 `std::string`，再一次 `output_.push(contiguous)`。成功写入后，必须从 `pending_` 删除这些位置。

这一步结束后再调用 `close_if_complete()`。

## 阶段 4：到达 EOF 时关闭 Writer

关闭条件只有：

```text
eof_index 已知 && first_unassembled() == eof_index
```

此时调用：

```cpp
output_.close();
```

不需要等待缓冲区为空。关闭只表示不会再有新字节；Reader 仍可读取已有字节。只有关闭后又被 Reader 读空，`is_finished()` 才为 true。

## 八组测试分别观察什么

1. 顺序片段能立即交付；
2. 空洞后的片段先暂存，补洞后一起冲刷；
3. 重叠和重复位置只出现一次；
4. Reader 已经取走的逻辑前缀不会再次交付；
5. Reader `pop` 后窗口向右扩展，窗口外后缀需要以后重传；
6. 最后片段先到时只记录 EOF，补洞后关闭 Writer，但未读数据仍存在；
7. 空流可以用空的最后片段结束；
8. 很远的未来片段和窗口外后缀不能无限占用内存。

## 渐进提示

只在卡住时往下看。

<details>
<summary>提示 1：为什么不需要单独维护 first_unassembled_？</summary>

因为输出只接受从 0 开始的连续前缀，所以 `output_.bytes_pushed()` 就是下一个尚未交付的位置。

</details>

<details>
<summary>提示 2：如何避免一边遍历 map 一边失效？</summary>

可以先把连续字节复制到临时 `std::string` 并记住长度，成功 push 后，再按逻辑位置逐个 `erase`。

</details>

<details>
<summary>提示 3：为什么冲刷前不用再次减去 pending 大小？</summary>

因为 pending 中所有位置都已经限制在 `[first_unassembled, first_unassembled + available_capacity)` 内，最多占满当前可用窗口。把字节从 pending 移到 ByteStream 只是在两个容器之间转移占用。

</details>

## 完成标准

- `ctest --test-dir build -C Debug --output-on-failure` 报告八组测试通过；
- 乱序、空洞、重叠、重复和已交付前缀都处理正确；
- 当前保存量不超过容量；
- `is_last_substring` 只记录 EOF，真正连续到 EOF 才关闭 Writer；
- Writer 关闭时允许 ByteStream 仍有未读字节；
- 不要求实验报告，代码中保留能解释关键判断的注释即可。

做完后把测试结果发给我；如果失败，发第一条失败信息和相关实现即可。
