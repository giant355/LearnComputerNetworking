# Lesson 19 / Lab 6：Tiny Network Interface

> 预计时间：3～5 小时，可以分两次完成
>
> 前置：第 6～9 课、Lab 1，以及“最终目标 IP / 当前下一跳 IP / 当前链路目标 MAC”的区别
>
> 你只需修改：`src/tiny_interface.cc`。不要求实验报告，测试结果与必要的代码注释就是完成证据。

## 这次为什么不是重做 Lab 1

Lab 1 的上层直接告诉接口：

```text
请把 D1 发给 next_hop=192.168.1.1
```

所以它只需要解决：

```text
下一跳 IP → ARP → MAC → Ethernet 帧
```

这次上层只交来含有**最终目标 IP**的数据报。接口还必须先判断：

```text
最终目标和我同网段吗？
  ├─ 是 → 下一跳就是目标本身
  └─ 否 → 下一跳是默认网关
```

而且未知邻居可能长期不回复，等待队列也不能无限增长。因此完整状态机变成：

```text
IPv4 数据报
  ↓ 根据本机前缀选择下一跳
下一跳 IP
  ↓ 查该邻居的状态
有效映射？ ──是──> 制作 IPv4 Ethernet 帧
  │否
  ↓
有限等待队列 + ARP 请求/重试
  ↓ 收到 ARP
学习 IP→MAC → 只冲刷这个邻居的队列
  ↓ 一直没有回复
最多三次请求 → 丢弃等待数据报并计数
```

## 完成后你能观察到什么

1. 同网段目标直接 ARP 目标主机，跨网段目标 ARP 默认网关；
2. 两个不同的远端目标共用同一个网关状态；
3. 两个不同的直连邻居拥有彼此独立的缓存、计时和等待队列；
4. ARP 回复只冲刷对应下一跳的 FIFO 队列，最终目标 IP 保持不变；
5. 未解析邻居每 5 秒重试，三次仍失败就丢弃等待数据；
6. 每个邻居的等待队列有上限，满时发生队尾丢弃；
7. 收到的帧先按 FCS 和目标 MAC 过滤，再按 EtherType 分流；
8. ARP 缓存 30 秒失效，之后的新发送重新解析。

## 目录

```text
Lab6_TinyNetworkInterface/
├── CMakeLists.txt
├── README.md
├── src/
│   ├── tiny_interface.hh   # 类型、公共契约和内部状态；先读
│   └── tiny_interface.cc   # 只修改这里的 TODO 1～5
└── tests/
    └── lab6_tests.cc       # 11 组行为测试
```

公共函数已经写有 XML 风格注释。三个复杂入口的 TODO 内也有完整路线图；实现时可以把路线图逐行翻译成分支，不需要先在脑中记住整个状态机。

---

## 0. 编译并观察起始失败

在 Developer PowerShell for Visual Studio 中运行：

```powershell
cd "C:\Users\Lenovo\Desktop\learn\计算机网络\Labs\Lab6_TinyNetworkInterface"
cmake -S . -B build
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

若想逐项查看：

```powershell
.\build\Debug\lab6_tests.exe
```

Starter 应该可以编译，但多项测试失败，因为 TODO 暂时返回占位结果。先确认是测试失败，不是编译环境失败。

## 1. 动手前先预测

不要查正文，用一句话分别预测：

1. 本机是 `192.168.1.10/24`，目标是 `192.168.1.77`，ARP 应询问谁？
2. 目标改成 `203.0.113.20`，网关是 `192.168.1.1`，ARP 应询问谁？
3. 两份数据报的最终目标分别是 `203.0.113.20` 和 `198.51.100.8`，它们会进入同一个等待队列吗？为什么？
4. 等待 ARP 时继续无限收数据为什么危险？

把预测写成源文件附近的临时注释即可，不需要单独报告。

## 2. 先读懂新状态

### 2.1 地址不再使用字符串

`IPv4Address` 内部保存 32 位整数，这让前缀比较变得直接：

```cpp
struct IPv4Address { std::uint32_t value {}; };
```

`from_octets` 和 `to_string` 已经完成，不要求你处理字符串解析。

### 2.2 每个下一跳拥有一份 NeighborState

```cpp
struct NeighborState {
    std::optional<MacAddress> mac;
    std::uint64_t cache_expires_at_ms {};
    std::deque<Datagram> waiting;
    std::optional<std::uint64_t> last_request_ms;
    std::size_t request_attempts {};
};
```

`neighbors_[next_hop]` 相当于：

```text
以当前下一跳 IP 为键
  → 查这个邻居的 MAC、等待数据、上次请求时间和请求次数
```

两个远端最终目标可能映射到同一个网关键；两个直连目标则通常使用两个不同的键。

`IPv4AddressHash` 只是让 `IPv4Address` 可以作为 `unordered_map` 的键，已经完成，可以当作黑箱。

---

## 3. TODO 1：判断是否同网段

实现：

```cpp
bool NetworkInterface::same_network(IPv4Address other) const
```

需要构造前缀掩码，然后比较：

```text
ip_ & mask
other & mask
```

边界要单独处理：

- `/0` 没有固定前缀位，本 Lab 中所有地址都匹配；
- `/32` 固定全部 32 位，只有地址完全相等才匹配；
- 其他长度的掩码可由全 1 左移 `32-prefix_length_` 得到。

完成后，`next_hop_for` 仍未实现，所以测试可能继续失败。

## 4. TODO 2：选择下一跳

实现：

```cpp
std::optional<IPv4Address> NetworkInterface::next_hop_for(IPv4Address destination) const
```

规则只有三条：

```text
same_network(destination) → destination
不直连且有 default_gateway_ → default_gateway_
不直连且没有网关 → nullopt
```

这里返回的是**当前下一跳**，绝不改写 `datagram.destination`。

完成后直接运行测试程序，第一组路由测试应通过。

## 5. TODO 3：发送数据报

实现 `send_datagram`。代码中的路线图已经给出全部分支，建议严格按顺序写。

### 5.1 没有路由

`next_hop_for` 返回空时：

```text
dropped_count_ 增加 1
返回 SendResult::no_route
```

### 5.2 缓存命中

取得 `neighbors_[next_hop]`。如果 `has_live_mapping(state)` 为真，使用已经完成的 `emit_ipv4_frame`，返回 `frame_ready`。

### 5.3 缓存未命中

先检查这个邻居的 `waiting.size()` 是否已经达到 `max_waiting_per_neighbor_`。满时只丢弃新来的数据报，不清空旧队列。

还有空间时，把数据报移入队尾。如果这个邻居还没有发送过请求：

```text
emit_arp_request(next_hop)
last_request_ms = now_ms_
request_attempts = 1
```

随后返回 `waiting_for_arp`。

注意：不能用 `last_request_ms.value_or(0)==0` 判断“从未请求”。第一次请求完全可能发生在 `now_ms_=0`；应直接检查 `has_value()`。

## 6. TODO 4：接收 Ethernet 帧

实现 `recv_frame`，先做共同过滤：

```text
FCS 失败 → 忽略
目标 MAC 既不是本机也不是广播 → 忽略
```

然后按 EtherType 分流。

### IPv4

本 Lab 不实现 IPv4 广播，所以只有：

```text
type==ipv4
destination==mac_
datagram 有值
```

三项同时满足才向上返回数据报。

### ARP

`frame.arp` 有值时：

1. 学习 `sender_ip → sender_mac`；
2. 冲刷 `sender_ip` 对应的等待队列；
3. 如果它是询问 `ip_` 的请求，调用 `emit_arp_reply`。

先学习再冲刷，因为制作等待中的 IPv4 帧需要刚学到的 MAC。

## 7. TODO 5：时间、重试与放弃

实现 `tick`。代码中的路线图对应四种状态：

```text
缓存到期                         → 清除 mac
没有等待数据                     → 不需要 ARP
等待中，但距离上次请求不足 5 秒  → 继续等
等待中，已到重试时刻             → 重试或最终放弃
```

第一次请求已经由 `send_datagram` 发出。因此：

```text
t=0s   第 1 次请求
t=5s   第 2 次请求
t=10s  第 3 次请求
t=15s  仍无回复，丢弃等待队列
```

最终放弃时：

- `dropped_count_` 增加等待队列原有的数量；
- 清空该队列；
- 清空 `last_request_ms`；
- 把 `request_attempts` 归零。

不要在遍历 `neighbors_` 时随便删除当前元素；本 Lab 不要求删除空状态，清空内部字段即可。

---

## 8. 十一组测试分别保护什么

| 测试 | 保护的机制 |
| --- | --- |
| Route chooses direct host or gateway | `/0`、`/24`、`/32` 与当前下一跳 |
| Missing gateway reports no route | 接口不能凭空猜路由 |
| Direct target is the ARP key | 同网段时询问目标本身 |
| Remote destinations share gateway state | 最终目标不同也可共用下一跳 |
| ARP reply learns and flushes FIFO | 学习、冲刷顺序与最终目标不变 |
| Unknown neighbors stay independent | 一个 ARP 回复不能冲刷其他邻居 |
| Receive filters before IPv4 delivery | FCS、目标 MAC、EtherType 分流 |
| ARP request learns and replies for self | 从请求学习，只替自己的 IP 回答 |
| Tick retries then drops unresolved neighbor | 请求间隔、次数与最终失败 |
| Waiting queue has per-neighbor capacity | 有限队列和队尾丢弃 |
| Cache expiry requires resolution again | 缓存生命周期 |

全部完成后直接运行测试程序，应看到：

```text
11 test(s) passed
```

## 9. 渐进提示

只有卡住时再展开下一层。

### 提示 A：前缀掩码

```text
/24 mask = 11111111 11111111 11111111 00000000
```

在 C++ 中，先用 `std::numeric_limits<std::uint32_t>::max()` 得到 32 个 1，再左移。

### 提示 B：不要复制 NeighborState

需要修改状态时取得引用：

```cpp
auto& state = neighbors_[next_hop];
```

如果写成 `auto state = ...`，修改的是副本。

### 提示 C：ARP 回复冲刷哪个键

不是 `frame.arp->target_ip`，而是刚刚学到的：

```text
frame.arp->sender_ip
```

因为等待的是“谁的 MAC 还不知道”。

### 提示 D：tick 中的时间差

只有 `last_request_ms.has_value()` 后才计算：

```text
now_ms_ - *last_request_ms
```

本 Lab 的测试不会让 `uint64_t` 时钟绕回。

## 10. 完成标准

不写实验报告，也不用统计分项用时。完成后发给我：

1. `ctest` 的最终摘要；
2. 你修改后的 `tiny_interface.cc`；
3. 若遇到值得记住的 bug，可以直接留在代码注释里。

我会检查：

- 11 组测试是否通过；
- 五个 TODO 的分支是否保持状态不变量；
- 是否把最终目标、下一跳和 MAC 分开；
- 注释是否准确，不以注释数量评分。

下一阶段不会立刻增加更多 ARP 规则，而会把这张接口接到最长前缀路由器上。
