# Lesson 15 / Lab 5：Tiny TCP Sender

> 预计时间：3–5 小时，可以分两次完成
>
> 前置阅读：[[../../14_TCP发送端_滑动窗口与重传|第 14 课：TCP 发送端]]、[[../Lab4_TinyTCPReceiver/README|Lab 4：Tiny TCP Receiver]]
>
> 你只需修改：`src/tiny_tcp_sender.cc`。不要求实验报告；测试通过并保留有意义的实现注释即可。

## 这次要造什么

应用已经把字节写进发送 ByteStream，但它们还没有自动变成可靠的 TCP 段。本 Lab 要实现这个发送端状态机：

```text
发送 ByteStream
  ↓ push：受窗口和 MSS 限制
带 SEQ 的新 TCP 段
  ├─ 副本保存到 outstanding
  └─ 原段进入待发送队列，随后交给 IP

接收 ACK + window
  ↓ receive
清理已确认副本、滑动窗口、继续发送

时间经过
  ↓ tick
RTO 到期：重传最旧 outstanding
```

你不会实现完整操作系统 TCP。本 Lab 只保留第 14 课真正需要观察的状态：

```text
next_seqno_abs、acknowledged_abs、advertised_window
outstanding、timer、RTO、连续重传次数
```

## 目录与阅读顺序

```text
Lab5_TinyTCPSender/
├── CMakeLists.txt
├── README.md
├── src/
│   ├── tiny_stream.hh/.cc          # 你在 Lab 2 完成的版本，不修改
│   ├── wrapping_integers.hh/.cc    # 你在 Lab 4 完成的版本，不修改
│   ├── tiny_tcp_sender.hh          # 先读消息、公共函数和成员状态
│   └── tiny_tcp_sender.cc          # 只修改这个文件
└── tests/
    └── lab5_tests.cc               # 十一组行为测试
```

推荐顺序：

1. 先读 `TcpSenderSegment`、`TcpReceiverMessage` 和所有成员变量；
2. 完成 `sequence_length()`；
3. 完成 `push()`，先通过 SYN、窗口填充和 FIN 测试；
4. 完成 `receive()`，通过累计 ACK 与窗口更新测试；
5. 完成 `tick()`，通过重传、退避和零窗口测试。

## 编译与第一次运行

在 Developer PowerShell for Visual Studio 中运行：

```powershell
cd "C:\Users\Lenovo\Desktop\learn\计算机网络\Labs\Lab5_TinyTCPSender"; cmake -S . -B build; cmake --build build --config Debug; ctest --test-dir build -C Debug --output-on-failure
```

在 WSL 中运行：

```bash
cd "/mnt/c/Users/Lenovo/Desktop/learn/计算机网络/Labs/Lab5_TinyTCPSender" && cmake -S . -B build && cmake --build build -j && ctest --test-dir build --output-on-failure
```

起始代码应能成功编译，但十一组测试都会失败。这表示测试已经看到占位实现，不是构建环境出错。

## 动手前先看懂三种容器

### 发送 ByteStream

它保存应用写入、但 TCP 尚未取走的字节：

```text
应用 push "ABCD" → ByteStream = ABCD
TCP 取走 ABC     → ByteStream = D
```

TCP 从这里 `peek()` 再 `pop()`，不会直接访问内部容器。

### `outstanding_`

它保存已经发送、但尚未被累计 ACK 完整覆盖的段副本：

```text
front → 最旧未确认段
back  → 最近发送的新段
```

收到推进 ACK 时从 `front` 清理；超时时也重传 `front`。因此使用 `std::deque`。

### `segments_to_send_`

它是准备交给下层的输出队列。新发送和重传都会把段放进这里；调用 `take_segments_to_send()` 后，这个队列会清空，但 `outstanding_` 中的副本仍然存在。

> [!important]
> 不要把两个队列混为一谈。`segments_to_send_` 回答“现在有什么要交给 IP”；`outstanding_` 回答“以后如果没收到 ACK，我还需要保留什么”。

## 开始前的四个预测

不用写报告，先在脑中判断：

1. 初始窗口按 1 处理时，为什么第一次 `push()` 只能发送 SYN？
2. `take_segments_to_send()` 清空后，为什么 `outstanding_count()` 不应变成 0？
3. 重传一个长度为 3 的旧段，为什么 `next_seqno_abs` 和 `bytes_in_flight` 都不增加？
4. ACK 没有前进，但 window 从 2 变成 4，发送端有没有可能发送新数据？

## 阶段 1：计算段占用的序号长度

实现：

```cpp
std::size_t TcpSenderSegment::sequence_length() const;
```

关系是：

```text
序号空间长度 = payload.size() + SYN占位 + FIN占位
```

例如 `SYN=1、payload="ABC"、FIN=1` 占 `1+3+1=5` 个位置。

纯 ACK 不属于这里的 `TcpSenderSegment`，本 Lab 的发送段只模拟会占序号空间、需要可靠确认的内容。

## 阶段 2：用 `push()` 填充窗口

### 2.1 先算还能占多少位置

正常窗口：

```text
window_right = acknowledged_abs_ + advertised_window_
available = window_right - next_seqno_abs_
```

当接收端通告 0 时，为避免双方永远沉默，本 Lab 暂时按 1 个探测位置处理：

```text
effective_window = advertised_window_ == 0 ? 1 : advertised_window_
```

这不是把接收端真正的容量改成 1，而是允许发送端试探一次。

### 2.2 一份新段按什么顺序装内容

在同一份段中按序号顺序考虑：

```text
SYN → payload → FIN
```

- SYN 只发送一次，并占一个位置；
- payload 从 ByteStream 头部取得，每段最多 `mss_` 字节，也不能越过窗口；
- 只有 Writer 已关闭、ByteStream 已被取空、窗口还有一个位置时，才能加入 FIN；
- FIN 也只发送一次。

如果最后没有装入任何内容，不要制造长度为 0 的段，否则循环无法前进。

### 2.3 新段必须同时进入两处

假设新段内部起点是 `absolute_start`：

```text
segment.seqno = wrap(absolute_start, isn_)
```

随后：

```text
保存一份到 outstanding_
放一份到 segments_to_send_
next_seqno_abs_ += segment.sequence_length()
```

如果发送前 `outstanding_` 为空，这份段就是新的“最旧未确认段”，此时才启动计时器并把已计时长度设为 0。后续追加新段不能让旧段重新获得完整等待时间。

## 阶段 3：用 `receive()` 处理 ACK 与窗口

收到的 `ackno` 是 32 位线上值：

```text
ack_abs = unwrap(ackno, isn_, next_seqno_abs_)
```

这里的 checkpoint 使用发送端已经知道的最近进度 `next_seqno_abs_`。

### 3.1 先拒绝不可能的 ACK

如果：

```text
ack_abs > next_seqno_abs_
```

它确认了发送端从未发送的位置。本 Lab 要忽略整份消息，不能让它顺便带来的巨大窗口改变发送状态。

### 3.2 合法消息先更新窗口

合法 ACK 即使没有前进，也可能携带新的 `window_size`。例如应用 Reader 读走数据后：

```text
ACK 仍为 1001
window 从 2 扩大到 4
```

这时 ACK 没有确认新字节，但扩大后的窗口可能允许继续发送。因此更新窗口后仍要尝试 `push()`。

### 3.3 只有新 ACK 才重置重传状态

满足：

```text
ack_abs > acknowledged_abs_
```

才是推进 ACK。此时：

1. 更新 `acknowledged_abs_`；
2. 从队首删除终点 `<= ack_abs` 的完整 outstanding 段；
3. `current_rto_ms_` 恢复初始值；
4. 连续重传次数清零；
5. 若仍有 outstanding，计时从 0 盯住新的队首；否则停止计时。

本 Lab 的测试只在段边界上确认，不要求切割“只确认了半个段”的 outstanding 副本。

## 阶段 4：用 `tick()` 驱动重传

如果没有未确认段，时间经过不会产生任何动作。否则累加：

```text
timer_elapsed_ms_ += elapsed_ms
```

尚未到 RTO 就继续等待；到达 RTO 时：

```text
把 outstanding_.front().segment 的副本放进 segments_to_send_
```

注意不要再次加入 `outstanding_`，也不要推进 `next_seqno_abs_`。这是重做旧账，不是第一次分配新位置。

若接收端通告的真实窗口非 0：

```text
consecutive_retransmissions_ += 1
current_rto_ms_ *= 2
```

若真实窗口为 0，本次属于零窗口探测，不增加连续重传次数，也不做指数退避。两种情况下，本轮计时都从 0 重新开始。

## 十一组测试分别观察什么

1. payload、SYN、FIN 的序号空间长度；
2. 初始一格窗口只能容纳 SYN；
3. ACK 打开窗口后，MSS 与窗口共同决定分段；
4. 累计 ACK 只删除被完整覆盖的 outstanding；
5. 重复 ACK 仍可通过扩大窗口释放发送资格；
6. RTO 到期只重传最旧段，并进行指数退避；
7. 新 ACK 重置 RTO，计时器改为盯住新的最旧段；
8. 零窗口探测会重传，但不做拥塞式退避；
9. FIN 等待窗口，并独占一个序号位置；
10. 线上 SEQ 回绕时，内部绝对位置仍持续增加；
11. 超过 `next_seqno_abs` 的不可能 ACK 被忽略。

## 渐进提示

只在卡住时展开。

<details>
<summary>提示 1：怎样同时限制 payload 的三个上限？</summary>

它不能超过 `mss_`、当前 ByteStream 缓冲量和本段剩余窗口。可以连续使用 `std::min`，不必写复杂循环。

</details>

<details>
<summary>提示 2：怎样判断一个 outstanding 段已被完整确认？</summary>

```text
segment_end = absolute_start + length
segment_end <= ack_abs → 可以删除
```

</details>

<details>
<summary>提示 3：怎样避免同一份新段移动两次后内容丢失？</summary>

先把段复制进一个队列，再 `std::move` 到另一个队列；或者两边都复制。段很小，本 Lab 先追求语义清楚。

</details>

<details>
<summary>提示 4：什么时候启动计时器？</summary>

新段保存前先记住 `outstanding_.empty()`。如果此前为空，保存后启动；如果此前已有段，保持原计时进度。

</details>

## 完成标准

- 十一组测试全部通过；
- 初始 SYN、payload 和 FIN 都正确占用序号空间；
- 新发送受窗口与 MSS 限制，重传不推进新序号；
- 累计 ACK 能清理 outstanding，新 ACK 与重复 ACK 的计时效果不同；
- 非零窗口超时执行指数退避，零窗口探测不退避；
- 线上序号回绕时内部位置仍正确；
- 不要求实验报告，关键分支保留必要注释即可。

完成后把测试结果发给我。如果失败，只需贴第一条失败信息和 `tiny_tcp_sender.cc` 中对应函数，不必贴整个工程。
