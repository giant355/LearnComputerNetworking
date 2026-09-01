# Lesson 4 / Lab 0：TinyNet 数据报路由器

> 预计时间：2.5–4 小时
> 环境：WSL2 或 Visual Studio Developer PowerShell、CMake、支持 C++20 的编译器
> 目标：亲手实现一个极小但行为清楚的 IP 数据报转发模型。

> [!tip]
> 如果 C++ 有些生疏，先阅读 [[04a_Lab0所需C++暖身|Lesson 4A：Lab 0 所需的 C++ 暖身]]。它只覆盖本 Lab 会遇到的容器、指针、`optional`、枚举和初始化写法，不包含 TODO 答案。

## 这次要造什么

我们不会调用真实网卡，也不会使用 Socket。整个网络存在于 C++ 内存里：

```text
Datagram
   ↓ Router::receive
检查 TTL → 查找精确目标路由 → 检查出口队列 → 入队
                                            ↓
                              Router::transmit_one
                                            ↓
                                      下一台路由器
```

完成后，下面这张小网络能够在测试中运行：

```text
A ── R1 ── R2 ── B
```

你会亲眼看到：

- TTL=3 的数据报能够经过 R1、R2 到达 B；
- TTL=2 的数据报在 R2 过期；
- 没有路由的数据报被丢弃；
- 容量为 2 的出口队列装不下第三个数据报；
- 队列按照先进先出顺序发送。

这不是工业级 IP 路由器。它是一块最小可运行模型，像 TinyRenderer 的第一组三角形：机制不全，但每一行都能解释。

## 学习目标

完成 Lab 后，你应该能够：

1. 把“尽力而为”落实成明确的成功与丢弃结果；
2. 正确实现 TTL 的逐跳减少与过期丢弃；
3. 区分最终目标地址、下一跳地址与出口接口；
4. 用有限 FIFO 队列观察排队和队尾丢弃；
5. 根据失败测试定位自己的模型违反了哪条不变量。

## 目录

```text
Lab0_TinyNet/
├── CMakeLists.txt
├── README.md
├── LAB0_WRITEUP.md
├── src/
│   ├── tiny_net.hh
│   └── tiny_net.cc
└── tests/
    └── lab0_tests.cc
```

你只需要修改：

```text
src/tiny_net.cc
LAB0_WRITEUP.md
```

不要为了通过测试修改测试文件。可以增加自己的测试。

---

## 0. 准备与第一次运行

这台电脑当前能够找到 Visual Studio C++ 工具链，但尚未安装可用的 WSL Linux 发行版。因此你可以先走 **Windows 路线**；以后安装 WSL 后再使用 Linux 路线，Lab 代码本身不需要改。

### 路线 A：Windows + Visual Studio Developer PowerShell

从开始菜单打开 **Developer PowerShell for Visual Studio**，然后进入目录：

```powershell
cd "C:\Users\Lenovo\Desktop\learn\计算机网络\Labs\Lab0_TinyNet"
```

运行：

```powershell
cmake -S . -B build
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

如果普通 PowerShell 提示找不到 `cmake`，不要急着安装第二份 CMake；Developer PowerShell 会自动配置 Visual Studio 自带工具的路径。

### 路线 B：WSL2

在 WSL 中进入目录：

```bash
cd "/mnt/c/Users/Lenovo/Desktop/learn/计算机网络/Labs/Lab0_TinyNet"
```

确认工具：

```bash
cmake --version
c++ --version
```

如果缺少，可以在 Ubuntu/WSL 中安装：

```bash
sudo apt update
sudo apt install build-essential cmake
```

配置、编译并运行测试：

```bash
cmake -S . -B build
cmake --build build -j
ctest --test-dir build --output-on-failure
```

初始代码应该能**编译成功、测试失败**。失败中会出现 `TODO`。这是正常起点，不要把 starter 的红灯当成环境失败。

如果编译本身失败，把第一条编译错误发给我；后面的错误常常只是连锁结果。

---

## 1. 先读数据模型

打开 `src/tiny_net.hh`。核心类型只有几个。

### 1.1 IPv4Address

```cpp
struct IPv4Address {
    std::uint32_t value {};
};
```

我们用一个 32 位无符号整数保存 IPv4 地址。`from_octets(10, 0, 0, 1)` 和 `to_string()` 已经完成，不是本 Lab 重点。

### 1.2 Datagram

```cpp
struct Datagram {
    IPv4Address source;
    IPv4Address destination;
    std::uint8_t ttl {64};
    std::uint8_t protocol {17};
    std::string payload;
};
```

这不是完整 IPv4 头部，只保留当前实验需要的字段：

- `source`：来源 IP；
- `destination`：最终目标 IP；
- `ttl`：还能经过多少路由器；
- `protocol`：上层类型，17 代表 UDP；本 Lab 不解析它；
- `payload`：上层载荷。

### 1.3 Route

```cpp
struct Route {
    IPv4Address destination;
    IPv4Address next_hop;
    std::size_t interface_index {};
};
```

它表达：

> 如果最终目标正好等于 `destination`，从 `interface_index` 出口发给 `next_hop`。

真实路由器使用网络前缀和最长前缀匹配。本 Lab 故意只做**精确目标匹配**，因为那部分还没有教。不要自行提前实现 CIDR。

### 1.4 OutboundPacket

```cpp
struct OutboundPacket {
    IPv4Address next_hop;
    Datagram datagram;
};
```

这里同时保存：

- IP 数据报仍然指向最终目标；
- 当前链路应该先交给哪个下一跳。

这正对应第 3 课中“最终目标”和“当前下一跳”不能合成一个字段。

---

## 2. 实现精确路由查找

在 `src/tiny_net.cc` 找到：

```cpp
const Route* Router::find_route(IPv4Address destination) const
```

遍历 `routes_`，返回第一个满足下面条件的路由地址：

```cpp
route.destination == destination
```

没有匹配时返回 `nullptr`。

### 这一阶段的不变量

- 不修改路由表；
- 不返回局部变量地址；
- 不猜测“最接近”的地址；
- 不匹配时必须明确返回 `nullptr`。

完成后重新编译。此时测试仍可能失败，因为 `receive` 还没有实现。

---

## 3. 实现接收与转发

找到：

```cpp
ReceiveResult Router::receive(Datagram datagram)
```

参数按值传入，这意味着函数拥有自己的数据报副本，可以安全减少副本的 TTL。

请严格按下面顺序实现。

### 3.1 检查 TTL

如果：

```cpp
datagram.ttl <= 1
```

返回：

```cpp
ReceiveResult::ttl_expired
```

不要放入任何出口队列。

为什么不是“先减再无条件转发”？因为 TTL 从 1 减到 0 后，路由器必须停止转发。

### 3.2 查找路由

调用 `find_route(datagram.destination)`。

没有路由时返回：

```cpp
ReceiveResult::no_route
```

### 3.3 检查接口与队列容量

`add_route` 已经保证接口索引有效，因此可以找到对应队列：

```cpp
queues_[route->interface_index]
```

如果队列当前大小已经等于容量，返回：

```cpp
ReceiveResult::queue_full
```

这就是最简单的队尾丢弃。

### 3.4 修改 TTL 并入队

只有确定可以转发后：

```cpp
--datagram.ttl;
```

创建 `OutboundPacket`，其中：

- `next_hop` 来自路由；
- `datagram` 是 TTL 已减少的数据报。

把它放到正确出口队列尾部，返回：

```cpp
ReceiveResult::forwarded
```

### 转发不变量

一次成功转发后应满足：

```text
来源 IP：不变
最终目标 IP：不变
载荷：不变
TTL：恰好减少 1
下一跳：来自匹配路由
队列数量：增加 1
```

一次丢弃后，任何出口队列都不应增加元素。

---

## 4. 实现接口发送一步

找到：

```cpp
std::optional<OutboundPacket> Router::transmit_one(
    std::size_t interface_index
)
```

行为：

1. 接口索引无效时抛出 `std::out_of_range`；
2. 队列为空时返回 `std::nullopt`；
3. 否则取出并删除队首元素，返回它。

这里使用 FIFO：先进入队列的数据先发出。

注意两个容易犯的错误：

- 返回了队首，却忘记 `pop_front()`；
- 先 `pop_front()`，再访问已经删除的引用。

安全写法的思路是：先把队首移动到局部变量，再删除队首，最后返回局部变量。

---

## 5. 运行测试并读懂每个失败

Windows + Visual Studio：

```powershell
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
.\build\Debug\lab0_tests.exe
```

WSL2：

```bash
cmake --build build -j
ctest --test-dir build --output-on-failure
./build/lab0_tests
```

最后一条是直接运行测试程序，通常比 `ctest` 的摘要更容易看出具体哪一项失败。

全部完成后应看到六项通过：

```text
[PASS] IPv4 address formatting
[PASS] TTL expiration
[PASS] No matching route
[PASS] Successful forwarding decrements TTL
[PASS] FIFO queue and tail drop
[PASS] Two-router path
6 test(s) passed
```

### 测试分别证明什么

| 测试 | 保护的不变量 |
| --- | --- |
| IPv4 address formatting | 地址模型按四个八位组保存和显示 |
| TTL expiration | TTL≤1 不会被继续转发 |
| No matching route | 路由器不会凭空猜下一跳 |
| Successful forwarding | TTL 恰减 1，其他端到端数据保持 |
| FIFO queue and tail drop | 有限队列、先进先出、满时丢弃 |
| Two-router path | 同一个数据报可以逐跳交给下一台路由器 |

测试通过不代表解释完成。接着做实验报告。

---

## 6. 完成 LAB0_WRITEUP.md

不要复制本教程的句子。用自己的话回答：

1. 为什么 TTL=2 不能走完 R1、R2 两台路由器到 B？
2. `destination` 与 `next_hop` 在代码中为什么是两个字段？
3. 为什么 `receive` 返回 `forwarded` 仍不能证明最终 B 收到了？
4. 队列容量从 2 改成 100，丢包可能减少，但会引入什么问题？
5. 记录最难调试的一处、错误现象和最终原因。
6. 分别记录阅读、实现、调试和报告用时。

---

## 7. 允许的帮助与调试方式

你可以：

- 打印 TTL、目标地址、接口索引和队列大小；
- 增加自己的测试；
- 把编译错误、失败测试和你当前的思路发给我；
- 查 C++ 标准库容器与 `std::optional` 的用法。

在第一次尝试前，不建议搜索完整实现。这个 Lab 的价值就在于把四条规则翻译成几十行自己的代码。

如果卡住，请按下面格式告诉我：

```text
当前阶段：find_route / receive / transmit_one
失败测试：
第一条错误：
我的预期：
实际结果：
我怀疑的原因：
```

## 知识锚点

- 本 Lab 是教学模型：使用精确目标路由，没有实现 IPv4 序列化、校验和、ICMP、ARP、CIDR 与最长前缀匹配。
- TTL 的转发规则来自 IPv4 基本协议规范 [RFC 791](https://www.rfc-editor.org/rfc/rfc791.html)。
- 后续 Lab 会逐步替换这些简化假设，而不是把当前模型当成真实网络栈。

## 小结

1. 路由器逐个处理独立数据报，不替应用建立可靠连接。
2. TTL 过期、没有路由或出口队列满，都可以导致丢弃。
3. 最终 IP 目标与当前下一跳属于不同范围。
4. 成功进入出口队列只表示转发的一小步，不表示端到端送达。
5. 测试验证行为，实验报告验证你是否理解行为。

---

## 检查站

完成后把下面内容发给我：

1. `ctest --test-dir build --output-on-failure` 的最终摘要；
2. `LAB0_WRITEUP.md` 的六个回答；
3. 你修改过的三个函数，或仓库提交号；
4. 实际总用时。

我会审查实现与解释，再决定是补充调试课，还是进入下一理论单元。
