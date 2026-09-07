# Lesson 9 / Lab 1：Tiny ARP Network Interface

> 预计时间：1.5–2.5 小时
>
> 前置阅读：[[../../09_网络接口状态机|第 9 课：网络接口状态机]] 与 [[../../08_ARP与网络接口|第 8 课：ARP 与网络接口]]
>
> 你只需修改：`src/tiny_arp.cc` 与 `LAB1_WRITEUP.md`

## 这次要造什么

这不是一张真实网卡，也不是交换机。它是主机或路由器上的一张**逻辑网络接口**：上层给它 IPv4 数据报和下一跳 IP，它要么直接做 Ethernet 帧，要么先 ARP 并等待。

```text
IP 层
  │ send_datagram(D1, 网关 IP)
  ▼
NetworkInterface
  ├─ ARP 缓存命中 → IPv4 Ethernet 帧
  └─ 缓存未命中 → D1 排队 + ARP 广播
                                      │
Ethernet 链路 ───── recv_frame(ARP 回复) ┘
                         ↓
                    学到 MAC，冲刷等待队列
```

完成后，你会看到六个可观察结果：

1. 首份数据报缓存未命中时，进入待解析队列并发一份 ARP 广播；
2. 同一下一跳的第二份数据报只排队，不重复广播；
3. 收到 ARP 回复后，D1、D2 按原顺序变成两份 IPv4 帧；
4. 缓存命中时立即制作 IPv4 帧；
5. 接口收到属于自己的 IPv4 帧时向 IP 层交付，而不是扮演交换机；
6. 30 秒后缓存过期；之后真的有新数据时才再次 ARP。

## 目录与阅读顺序

```text
Lab1_TinyARPInterface/
├── CMakeLists.txt
├── README.md
├── LAB1_WRITEUP.md
├── src/
│   ├── tiny_arp.hh   # 数据模型和接口，先读，不修改
│   └── tiny_arp.cc   # 你要实现的 TODO
└── tests/
    └── lab1_tests.cc # 先看每个测试的中文注释
```

先读 `tiny_arp.hh` 的类型，再读测试，最后实现 `tiny_arp.cc`。测试里的 `NetworkInterface nic(local_ip, local_mac)` 表示“这是一张拥有本机 IP/MAC 的接口”。

---

## 0. 编译与第一次运行

在 **Developer PowerShell for Visual Studio**：

```powershell
cd "C:\Users\Lenovo\Desktop\learn\计算机网络\Labs\Lab1_TinyARPInterface"
cmake -S . -B build
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

在 WSL：

```bash
cd "/mnt/c/Users/Lenovo/Desktop/learn/计算机网络/Labs/Lab1_TinyARPInterface"
cmake -S . -B build
cmake --build build -j
ctest --test-dir build --output-on-failure
```

起始代码应当**编译成功、六项测试失败**。`TODO` 是待实现逻辑，不是环境错误。

若连编译都失败，先把第一条编译错误发给我，不要急着改测试。

---

## 1. 先读懂数据，不要急着写

为了避免地址解析和字节序列化遮住状态机，本 Lab 做了三项简化：

```text
IP/MAC 地址：字符串，例如 "10.0.0.1"、"GG:GG:GG:GG:GG:GG"
Ethernet 帧：C++ 结构体，不做真实字节编码
时间：测试显式调用 tick(毫秒)
```

### 1.1 三个重要容器

```cpp
std::unordered_map<IpAddress, CacheEntry> arp_cache_;
std::unordered_map<IpAddress, std::deque<Datagram>> waiting_;
std::unordered_map<IpAddress, std::uint64_t> last_arp_request_ms_;
```

可以把 `unordered_map` 当作 C# 的 `Dictionary<TKey, TValue>`：用键快速找值。这里的键都是下一跳 IP。

`std::deque<Datagram>` 是双端队列；本 Lab 只从尾部加入、从头部取出，所以它作为 FIFO 使用。

你不需要修改 `send_ipv4_frame`、`send_arp_request`、`learn_arp_mapping`、`flush_waiting_datagrams`。先把它们当作已经完成的小积木：前两个负责造帧，第三个更新缓存，第四个把一整个等待队列变成 IPv4 帧。

---

## 2. 阶段一：发送数据报——命中或等待

实现 `send_datagram`。

先写出你要保持的两条不变量：

```text
缓存命中：不增加 waiting_，只增加一份 IPv4 帧。
缓存未命中：增加 waiting_[next_hop]，但不一定每次都增加 ARP 请求帧。
```

### 2.1 缓存命中

`cache_has_live_entry(next_hop)` 为真时：

1. 从 `arp_cache_` 取出 MAC；
2. 调用已完成的 `send_ipv4_frame`；
3. 立即 `return`。

### 2.2 缓存未命中

先把数据报放到队尾：

```cpp
waiting_[next_hop].push_back(std::move(datagram));
```

然后看 `last_arp_request_ms_`：若该 IP 从未请求，或距离上次请求已经至少 5000 ms，调用 `send_arp_request(next_hop)` 并记录 `now_ms_`。

这里不要误写成“每次未命中都请求”。D1、D2 的关键正是共用一份请求。

阶段完成后运行测试；第一项应能通过。

---

## 3. 阶段二：收到 ARP——学习并冲刷

实现 `recv_frame` 的 ARP 分支前，先实现最外层过滤：

```text
目标 MAC 既不是 mac_ 也不是 broadcast_mac → return nullopt
FCS 失败 → return nullopt
```

然后按 `frame.type` 分支。

### 3.1 IPv4 分支

这是主机/路由器接口的接收行为：如果 `frame.datagram` 有值，直接返回它。

不要调用 `send_ipv4_frame`，不要查交换机表。接口的任务是把这份有效 IPv4 数据报交给本机 IP 层。

### 3.2 ARP 分支

ARP 帧应先确认 `frame.arp` 有值。随后：

```text
learn_arp_mapping(发送者 IP, 发送者 MAC)
flush_waiting_datagrams(发送者 IP)
```

这个顺序不能反：`flush_waiting_datagrams` 需要先从 ARP 缓存取 MAC，才能制作 IPv4 帧。

完成后，第二、三项测试会通过。

---

## 4. 阶段三：收到询问自己 IP 的 ARP 请求

如果 ARP 帧类型是 `arp_request`，并且：

```text
frame.arp->target_ip == ip_
```

接口拥有被询问的 IP，因此应把一份 **ARP 回复帧**放入 `frames_to_send_`：

```text
Ethernet 目标 MAC = 请求者 MAC
Ethernet 来源 MAC = mac_
类型 = arp_reply

ARP 发送者 IP/MAC = ip_ / mac_
ARP 目标 IP = 请求者 IP
```

这里用和 `send_arp_request` 完全相同的 `EthernetFrame { ... }` 写法即可，只是字段值不同。查看第 8 课第 5 节的 ARP 回复图，不需要搜索完整答案。

完成后第五项测试通过。

---

## 5. 阶段四：时间流逝与缓存失效

实现 `tick`：

1. `now_ms_ += elapsed_ms`；
2. 遍历 `arp_cache_`，删除 `expires_at_ms <= now_ms_` 的项。

遍历 `unordered_map` 并删除元素时，不能在普通 `for (++it)` 循环中直接 `erase(it)` 后继续使用旧迭代器。安全结构是：

```cpp
for (auto it = arp_cache_.begin(); it != arp_cache_.end();) {
    if (/* 已到期 */) {
        it = arp_cache_.erase(it);
    } else {
        ++it;
    }
}
```

这段不是网络协议难点，只是 C++ 容器在删除时如何保持迭代器有效。最后一项测试验证：到期后不会自动发 ARP；只有 D4 真的到来，才排队并产生新请求。

---

## 6. 全部通过时的输出

```text
[PASS] Cache miss queues and broadcasts once
[PASS] ARP reply learns and flushes queue
[PASS] Cache hit sends IPv4 immediately
[PASS] IPv4 reception is not switch forwarding
[PASS] ARP request learns and replies
[PASS] Cache expiry requires new request
6 test(s) passed
```

若第一项失败，先检查 `send_datagram` 是否同时完成“排队”和“首次请求”。若第二项失败，检查是否在学习映射后调用 `flush_waiting_datagrams`。若第四项失败，不要去改交换机逻辑；这里只有主机/路由器接口。

## 7. 完成报告

填写 [LAB1_WRITEUP.md](LAB1_WRITEUP.md)。仍采用精简证据链：测试摘要、三句核心解释、一个真实 bug（若有）、实际用时。

完成后发给我：

1. `ctest` 最终输出；
2. `LAB1_WRITEUP.md` 的内容；
3. `tiny_arp.cc` 的修改，或提交号；
4. 总用时。

我会先检查状态机是否正确，再看你的解释；两者都通过才算 Lab 完成。
