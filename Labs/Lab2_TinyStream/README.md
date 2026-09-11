# Lesson 11 / Lab 2：TinyStream

> 预计时间：1.5–2.5 小时
>
> 前置阅读：[[../../11_有限容量字节流|第 11 课：有限容量字节流]]
>
> 你只需修改：`src/tiny_stream.cc`。不要求单独实验报告；测试结果和必要代码注释就是证据。

## 这次要造什么

上一课先在纸上造了一个模型：

```text
Writer → [有限容量的未读字节] → Reader
```

这次把模型写成 C++。它不是 TCP，也不发送真实网络数据；它只实现 TCP 以后会依赖的本地字节流契约。

```text
push  → 尾部追加，空间不足时部分接受
peek  → 查看头部，不消费
pop   → 从头部消费，释放容量
close → 禁止未来写入，但保留未读字节
```

完成后，七组测试会观察：

1. 字节顺序与 `peek`/`pop` 行为；
2. 容量满时的部分写入；
3. 关闭后排空与拒绝新数据；
4. 空但开放不等于 finished；
5. 超量 `pop` 的边界与累计计数；
6. 值为 `0` 的二进制字节；
7. 与你刚完成的容量为 4 状态迁移完全相同的 FIFO 追踪。

## 目录与阅读顺序

```text
Lab2_TinyStream/
├── CMakeLists.txt
├── README.md
├── src/
│   ├── tiny_stream.hh   # 接口和必要 XML 注释，先读，不修改
│   └── tiny_stream.cc   # 你要实现的 TODO
└── tests/
    └── lab2_tests.cc    # 七组可观察行为，先读测试再写代码
```

先读头文件，再读测试名称和断言，最后实现 `tiny_stream.cc`。测试不要求你猜隐藏状态：每个阶段都能从公开函数观察结果。

## 编译与第一次运行

在 Developer PowerShell for Visual Studio 中运行：

```powershell
cd "C:\Users\Lenovo\Desktop\learn\计算机网络\Labs\Lab2_TinyStream"; cmake -S . -B build; cmake --build build --config Debug; ctest --test-dir build -C Debug --output-on-failure
```

在 WSL 中运行：

```bash
cd "/mnt/c/Users/Lenovo/Desktop/learn/计算机网络/Labs/Lab2_TinyStream" && cmake -S . -B build && cmake --build build -j && ctest --test-dir build --output-on-failure
```

起始代码应当**编译成功、七组测试失败**。这是因为函数暂时返回占位值，不是环境错误。

## 阶段一：先实现容量和 `push`

实现 `available_capacity()`、`bytes_buffered()`、`bytes_pushed()`，再实现 `push`。

每次 push 的最大接受量是：

```text
min(data.size(), available_capacity())
```

只把接受的前缀复制到 `buffer_`，并把实际数量加到 `bytes_pushed_`。不要把未接受的后缀偷偷算进计数器。

阶段一的核心不变量是：

```text
0 ≤ bytes_buffered() ≤ capacity_
available_capacity() = capacity_ - bytes_buffered()
```

## 阶段二：实现 `peek` 与 `pop`

`peek(max_len)` 复制头部最多 `max_len` 个字节，不修改任何状态。

`pop(len)` 从 `buffer_` 头部移除最多 `len` 个字节，返回实际移除量，并增加 `bytes_popped_`。如果 `len` 大于当前缓冲量，只能移除现有字节。

每次成功 pop 后检查：

```text
bytes_buffered() == bytes_pushed() - bytes_popped()
```

## 阶段三：实现关闭和结束状态

`close()` 只设置 `closed_ = true`，不能清空 `buffer_`。

```text
is_finished() == is_closed() && bytes_buffered() == 0
```

关闭后 `push` 必须返回 0。关闭但仍有未读字节时，Reader 仍然可以 `peek` 和 `pop`。

## 阶段四：用测试反查边界

推荐顺序：

1. 先让 `push, peek, and pop` 通过；
2. 再处理部分写入和容量；
3. 再处理 close/finished；
4. 最后检查二进制 `\0` 和完整状态追踪。

不要一开始就实现环形缓冲区。`std::deque<char>` 已经直接支持 `push_back` 和 `pop_front`，先把契约写对；优化属于后续工作。

## 允许使用的实现工具

- `std::deque<char>`：从尾部加入，从头部删除；
- `std::string`：构造 `peek` 的返回副本；
- `std::string_view`：只在 `push` 调用期间读取输入；
- `std::min`：计算实际接受或消耗的数量。

不需要加入线程、锁、条件变量、网络套接字或环形缓冲区。它们会改变问题范围。

## 通过标准

代码完成标准：

- `ctest --test-dir build -C Debug --output-on-failure` 报告 7 test(s) passed；
- `push` 从不超过容量；
- `peek` 不改变状态；
- `close` 不丢弃已接受数据；
- 两个容量/计数不变量在测试序列中成立；
- 公共函数的 XML 注释保留并与实际契约一致。

概念完成标准：你能解释“容量限制的是当前未读字节，不是生命周期总量”，并能手工追踪一个 `push → pop → push → close → pop` 序列。

