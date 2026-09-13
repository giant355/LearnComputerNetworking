# Lesson 13 / Lab 4：Tiny TCP Receiver

> 预计时间：2.5–4 小时
>
> 前置阅读：[[../../13_TCP接收端_序号_ACK与窗口|第 13 课：TCP 接收端]]、[[../Lab3_TinyReassembler/README|Lab 3：TinyReassembler]]
>
> 你只需修改：`src/wrapping_integers.cc` 和 `src/tiny_tcp_receiver.cc`。不要求实验报告；测试通过并保留有意义的实现注释即可。

## 这次要造什么

Lab 3 的 Reassembler 已经能接收：

```text
(first_index, payload, is_last_substring)
```

真实 TCP 接收端收到的却是：

```text
(32 位 SEQ, SYN, FIN, payload)
```

本 Lab 要在两者之间增加翻译层，并根据重组结果生成 ACK 与接收窗口：

```text
TcpSegment
  ↓ unwrap SEQ，跳过 SYN
first_index + payload + FIN
  ↓
TinyReassembler → TinyStream → 应用 Reader
  ↓
ACK + window
```

## 目录与阅读顺序

```text
Lab4_TinyTCPReceiver/
├── CMakeLists.txt
├── README.md
├── src/
│   ├── tiny_stream.hh/.cc          # 你在 Lab 2 完成的版本，不修改
│   ├── tiny_reassembler.hh/.cc     # 你在 Lab 3 完成的版本，不修改
│   ├── wrapping_integers.hh        # 先读 XML 注释
│   ├── wrapping_integers.cc        # 阶段 1、2
│   ├── tiny_tcp_receiver.hh        # 再读状态和公共契约
│   └── tiny_tcp_receiver.cc        # 阶段 3、4
└── tests/
    └── lab4_tests.cc               # 十组行为测试
```

推荐顺序：

1. 阅读两个新头文件中的 XML 注释；
2. 只看测试函数名称，预测每组在检查什么；
3. 完成 `wrap`；
4. 完成 `unwrap`；
5. 完成 `receive`；
6. 完成 `ackno` 和 `window_size`。

## 编译与第一次运行

在 Developer PowerShell for Visual Studio 中运行：

```powershell
cd "C:\Users\Lenovo\Desktop\learn\计算机网络\Labs\Lab4_TinyTCPReceiver"; cmake -S . -B build; cmake --build build --config Debug; ctest --test-dir build -C Debug --output-on-failure
```

在 WSL 中运行：

```bash
cd "/mnt/c/Users/Lenovo/Desktop/learn/计算机网络/Labs/Lab4_TinyTCPReceiver" && cmake -S . -B build && cmake --build build -j && ctest --test-dir build --output-on-failure
```

起始代码应当能够编译，但测试失败。因为四个核心函数仍是占位实现，这不是环境错误。

## 开始前的三个预测

不需要写报告，在脑中回答即可：

1. `wrap(2^32+6, ISN=1000)` 为什么仍然得到 1006？
2. 后半段先到时，`bytes_pending` 增加，ACK 为什么不一定前进？
3. 应用 Reader `pop` 后，为什么窗口会扩大而 ACK 不改变？

## 阶段 1：实现 wrap

目标公式：

```text
线上 SEQ = (ISN + 内部绝对序号) mod 2^32
```

C++ 的 `std::uint32_t` 无符号加法天然按 `2^32` 回绕。你只需把 64 位绝对序号的低 32 位和 `zero_point.raw_value()` 相加。

不要在这里使用 `% UINT32_MAX`：模数是 `2^32`，而 `UINT32_MAX` 是 `2^32-1`。

## 阶段 2：实现 unwrap

先求本圈内相对于 ISN 的 32 位偏移：

```text
offset = value - zero_point       // uint32_t 自然回绕
```

同一个 offset 可能对应：

```text
offset
offset + 2^32
offset + 2 × 2^32
...
```

令：

```text
cycle = checkpoint / 2^32
```

只需要比较 checkpoint 所在圈附近的候选：

```text
cycle - 1
cycle
cycle + 1
```

选择绝对距离最小的一个；若距离相同，测试要求选择较小者。构造候选前要避免 `cycle+1` 或乘加超过 `uint64_t`。

## 阶段 3：实现 receive

### 3.1 先建立 ISN

合法 SYN 到达前，接收端不知道序号原点，所以忽略普通数据段：

```text
isn_ 为空且 segment.syn=false → return
```

首次合法 SYN 到达时：

```text
isn_ = segment.seqno
```

### 3.2 选择 checkpoint

重组器的下一个应用位置是：

```text
first_unassembled
```

内部绝对序号还包含 SYN，所以一个自然的参考点是：

```text
checkpoint = first_unassembled + 1
```

### 3.3 换算 payload 起点

```text
segment_absolute = unwrap(segment.seqno, isn, checkpoint)
payload_absolute = segment_absolute + (segment.syn ? 1 : 0)
first_index = payload_absolute - 1
```

如果 `payload_absolute==0`，不能执行无符号减一；这类没有 SYN、却声称位于 SYN 位置的段不应交给 Reassembler。

最后调用：

```cpp
reassembler_.insert(first_index, segment.payload, segment.fin);
```

注意：即使 payload 为空，FIN 仍然可能定义 EOF，不能只在 `payload` 非空时调用重组器。

## 阶段 4：生成 ACK 和窗口

SYN 到达前：

```text
ackno() = nullopt
```

SYN 到达后，内部 ACK 偏移为：

```text
1                       // SYN
+ bytes_pushed          // 已连续重组的 payload
+ (Writer closed ? 1:0) // 已连续到达的 FIN
```

再用 `wrap` 转换成线上 ACK。

本 Lab 明确规定总容量由两部分共同占用：

```text
window = capacity
       - ByteStream 未读字节
       - Reassembler 待重组字节
```

做减法前先取得两部分之和；若实现出现异常状态，不允许无符号下溢成巨大窗口，可以将结果钳制为 0。

## 十组测试分别观察什么

1. `wrap` 普通情况与完整一圈后的同余值；
2. `unwrap` 根据 checkpoint 选择最近圈；
3. SYN 前的数据被忽略；
4. SYN 建立 ISN，并让 ACK 变成 `ISN+1`；
5. SYN 与 payload 同段时，payload 仍从 `first_index=0` 开始；
6. 顺序 payload 推进 ACK 并占用窗口；
7. FIN 和后半段先到时 ACK 不越过空洞，补洞后一起确认；
8. Reader `pop` 只扩大窗口，不改变 ACK；
9. TCP SEQ 跨越 32 位边界时仍能正确重组；
10. SYN 与 FIN 可以描述一条空字节流。

## 渐进提示

只在卡住时展开。

<details>
<summary>提示 1：怎样比较两个 uint64_t 的距离而不使用浮点数？</summary>

```cpp
const auto distance = candidate > checkpoint ? candidate - checkpoint : checkpoint - candidate;
```

</details>

<details>
<summary>提示 2：unwrap 的候选怎样避免溢出？</summary>

把 `2^32` 保存为 `uint64_t`。先检查圈号是否不超过 `UINT64_MAX / modulus`，并检查偏移是否还能加到该圈起点上，再构造候选。

</details>

<details>
<summary>提示 3：为什么 ACK 用 bytes_pushed，不用 bytes_popped？</summary>

ACK 表示 TCP 已连续接管多少字节；应用读走数据不会抹掉“已经收到”这个事实。

</details>

<details>
<summary>提示 4：为什么窗口还要减 bytes_pending？</summary>

本课程的 TinyTCP 规定乱序暂存和 ByteStream 未读内容共享同一个总容量；从 pending 移到 ByteStream 只是占用位置转移，不应凭空增加容量。

</details>

## 完成标准

- 十组测试全部通过；
- `wrap/unwrap` 能正确处理至少一次 32 位回绕；
- SYN 前数据被忽略，SYN 与 payload 同段时位置正确；
- 累计 ACK 不越过空洞，并在连续到 FIN 后多确认一个序号；
- Reader 读取只改变窗口，不改变 ACK；
- 不要求实验报告，代码中的关键判断保留必要注释即可。

完成后把测试结果发给我。如果失败，只需贴第一条失败信息和两个新 `.cc` 中相关函数，不必贴整个工程。
