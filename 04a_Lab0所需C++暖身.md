# Lesson 4A：Lab 0 所需的 C++ 暖身

> 预计时间：30–50 分钟
>
> 目标：读懂 Lab 0 的数据结构、三个 TODO 的函数签名和测试用例
>
> 边界：这不是完整的 C++ 复习课，也不会给出三个 TODO 的实现

你觉得 Lab 代码难读，很大一部分原因不是网络概念本身，而是两种负担叠在了一起：

```text
新网络概念：数据报、TTL、路由、下一跳、出口队列
        +
生疏的 C++：模板容器、指针、optional、移动、聚合初始化
        ↓
看一小段代码也很累
```

这很正常。解决办法不是把整本 C++ 重新学一遍，而是先恢复 Lab 0 真正需要的那一小块语言能力。

## 学习目标

完成本课后，你应该能够：

1. 看懂 `Datagram`、`Route`、`OutboundPacket` 和 `Router` 保存了什么；
2. 解释 `Router router({2})`、`Route {host_b, next_hop, 0}` 等初始化写法；
3. 看懂 `vector<deque<OutboundPacket>>` 表示“多个出口队列”；
4. 区分指针可能为空与 `optional` 可能没有值；
5. 理解为什么 `receive(Datagram datagram)` 可以安全修改自己的 TTL 副本；
6. 把测试代码翻译成“准备—执行—检查”三步，而不被测试框架吓住。

---

## 0. 先决定哪些代码需要读

Lab 0 不是 C++ 阅读考试。不同代码的重要程度并不相同。

### 必须大致看懂

```text
src/tiny_net.hh 中的四个数据结构
Router 的成员变量
find_route、receive、transmit_one 的函数签名
每个测试准备了什么、期待什么结果
```

### 暂时可以当作工具

```text
IPv4Address::from_octets 中的移位和按位或
IPv4Address::to_string 中的 ostringstream
测试程序 main 中的 std::function 和异常捕获
CMakeLists.txt 的具体语法
```

这些工具代码已经完成并经过测试。现在不懂它们，不妨碍你实现路由器行为。

阅读代码时可以在脑中给它们贴标签：

```text
我现在必须理解
以后再研究
```

这是控制学习负担，不是逃避。

---

## 1. `struct`：先把它看成公开的数据盒子

Lab 中有：

```cpp
struct Datagram {
    IPv4Address source;
    IPv4Address destination;
    std::uint8_t ttl {64};
    std::uint8_t protocol {17};
    std::string payload;
};
```

它表示一个 `Datagram` 由五部分组成：

```text
Datagram
├── source：来源 IP
├── destination：最终目标 IP
├── ttl：还能经过多少路由器，默认 64
├── protocol：上层协议编号，默认 17
└── payload：上层载荷
```

在这个 Lab 里，可以把 C++ `struct` 暂时类比成一个字段公开的数据类型。

C# 中可能写成：

```csharp
public sealed class Datagram
{
    public IPv4Address Source;
    public IPv4Address Destination;
    public byte Ttl = 64;
    public byte Protocol = 17;
    public string Payload = "";
}
```

这个类比只用于理解数据形状。C++ 的 `struct` 与 C# 引用类型在复制和生命周期上并不完全相同。

### `{64}` 是默认值

```cpp
std::uint8_t ttl {64};
```

意思是：构造 `Datagram` 时如果没有明确提供 TTL，就把它初始化为 64。

这不是“容量为 64”，也不是一个数组。

### `struct` 与 `class` 的关系

在 C++ 中，它们都可以有字段、方法和构造函数。对当前 Lab 最重要的区别是默认访问权限：

- `struct` 成员默认是 `public`；
- `class` 成员默认是 `private`。

这里用 `struct` 是因为 `Datagram`、`Route` 和 `OutboundPacket` 主要负责装数据。

---

## 2. 花括号初始化：把值按字段装进去

测试中有：

```cpp
Route {host_b, next_hop, 0}
```

对照定义：

```cpp
struct Route {
    IPv4Address destination;
    IPv4Address next_hop;
    std::size_t interface_index {};
};
```

值会按声明顺序对应：

```text
destination     = host_b
next_hop        = next_hop
interface_index = 0
```

C# 对象初始化器更强调名字：

```csharp
new Route
{
    Destination = hostB,
    NextHop = nextHop,
    InterfaceIndex = 0
};
```

### C++20 的指定成员初始化

测试辅助函数还使用：

```cpp
return Datagram {
    .source = source,
    .destination = destination,
    .ttl = ttl,
    .protocol = 17,
    .payload = std::move(payload),
};
```

这里直接写出字段名，读法与 C# 对象初始化器很接近。

你不需要在 TODO 中照搬这种写法；只需要能看懂测试怎样制造数据报。

### 最容易误读的一行

```cpp
Router router({2});
```

`Router` 的构造函数需要：

```cpp
std::vector<std::size_t> interface_capacities
```

所以 `{2}` 表示一个只含一个元素的 `vector`：

```text
接口 0 的容量 = 2
```

它不是“路由器编号为 2”，也不是“路由器有两个接口”。

如果写：

```cpp
Router router({2, 5});
```

才表示：

```text
接口 0 的队列容量 = 2
接口 1 的队列容量 = 5
```

### 停一下

`Router router({3, 1, 4});` 有几个接口？每个接口的队列容量分别是多少？

先在心里回答，再继续。

---

## 3. 几个看起来陌生、实际很小的类型

### 固定宽度整数

```cpp
std::uint8_t
std::uint32_t
```

可以先读成：

```text
uint8_t  = 8 位无符号整数，类似 C# byte
uint32_t = 32 位无符号整数，类似 C# uint
```

IP 地址需要 32 位，TTL 只需要一个小范围整数，所以用了不同宽度。

### `std::size_t`

```cpp
std::size_t interface_index;
```

`size_t` 是 C++ 标准库常用来表示大小和索引的无符号整数类型。容器的 `size()` 也返回它。

当前 Lab 里把它当作“容器索引类型”即可。

### `auto`

```cpp
const auto host_b = ip(80, 0, 0, 1);
```

编译器根据右侧推断类型。这里 `host_b` 的实际类型是 `IPv4Address`。

`auto` 不代表动态类型。类型仍然在编译时确定。

### `const`

```cpp
const auto host_b = ...;
```

表示之后不通过这个变量修改它。

函数末尾的 `const`：

```cpp
std::size_t queued_count(std::size_t interface_index) const;
```

表示这个成员函数承诺不修改 `Router` 的成员状态。

### `[[nodiscard]]`

```cpp
[[nodiscard]] std::size_t queued_count(...) const;
```

它提醒调用者：这个返回结果大概很重要，别无意中忽略。它不改变算法含义。

### `= default`

```cpp
bool operator==(const IPv4Address&) const = default;
```

这里让编译器按字段生成相等比较。于是可以写：

```cpp
address_a == address_b
```

你不需要自己实现它。

---

## 4. 容器：一张路由表和多个出口队列

`Router` 的三个核心成员是：

```cpp
std::vector<Route> routes_;
std::vector<std::size_t> interface_capacities_;
std::vector<std::deque<OutboundPacket>> queues_;
```

### `vector<Route>`：可增长的路由数组

```cpp
std::vector<Route> routes_;
```

可以类比成 C# 的：

```csharp
List<Route> routes;
```

常用操作：

```cpp
routes_.push_back(route);  // 末尾增加一条
routes_.size();            // 有多少条
routes_.empty();           // 是否为空
```

### `deque<OutboundPacket>`：支持队首取出

`deque` 可以从尾部放入、从头部取出，适合表示 FIFO 队列：

```cpp
std::deque<std::string> tasks;

tasks.push_back("D1");
tasks.push_back("D2");

const auto first = tasks.front(); // 查看 D1
tasks.pop_front();                // 删除 D1
```

之后队列只剩：

```text
[D2]
```

### 最关键的嵌套类型

```cpp
std::vector<std::deque<OutboundPacket>> queues_;
```

从里面向外读：

```text
deque<OutboundPacket>          一个出口的数据报队列
vector<deque<OutboundPacket>>  许多个出口队列
```

可以画成：

```text
queues_
├── [0] → [D1, D2]
├── [1] → []
└── [2] → [D7]
```

所以：

```cpp
queues_[1]
```

得到接口 1 的整个队列；而：

```cpp
queues_[1].size()
```

得到接口 1 当前排了几个数据报。

### 一个安全习惯

对队列调用 `front()` 或 `pop_front()` 前，先检查：

```cpp
queue.empty()
```

空队列没有队首元素。

---

## 5. 引用与指针：这里为什么会出现 `const Route*`

函数签名：

```cpp
const Route* find_route(IPv4Address destination) const;
```

拆开读：

```text
find_route                         函数名
IPv4Address destination            输入一个目标地址
const Route*                       返回“指向只读 Route 的指针”
最后一个 const                     查找时不修改 Router
```

为什么返回指针？因为查找有两种结果：

```text
找到：指针指向 routes_ 中的一条 Route
没找到：返回 nullptr
```

### `nullptr`

```cpp
const Route* route = nullptr;
```

表示指针目前不指向任何对象。

使用指针前要确认它不是空指针：

```cpp
if (route != nullptr) {
    // 现在才能读取 route 指向的内容
}
```

### `->`：通过指针访问成员

如果 `route` 是 `Route*`：

```cpp
route->next_hop
```

等价于：

```cpp
(*route).next_hop
```

第一种更常用。

### 范围 `for` 中的引用

遍历路由表时，常见写法是：

```cpp
for (const auto& route : routes_) {
    // 读取 route
}
```

这里：

- `auto` 让编译器推断它是 `Route`；
- `&` 表示引用原元素，不复制整条路由；
- `const` 表示只读。

可以把 `const auto&` 暂时翻译为：

> 给我原对象的一个只读别名。

### 不要返回局部变量的地址

下面是错误思路：

```cpp
const Route* bad_example()
{
    Route temporary {};
    return &temporary;
}
```

函数结束后 `temporary` 已经消失，返回的地址失效。

Lab 中如果找到路由，应该指向 `routes_` 中已经存在的元素，而不是临时新造一条。

---

## 6. `optional`：明确表达“可能没有数据”

函数：

```cpp
std::optional<OutboundPacket> transmit_one(
    std::size_t interface_index
);
```

它有两种正常结果：

```text
队列有数据：返回一个 OutboundPacket
队列为空：返回 std::nullopt
```

`optional<T>` 可以理解成一个盒子：

```text
有值：[T]
无值：[]
```

### 一个与 Lab 无关的小例子

```cpp
std::optional<std::string> find_name(bool exists)
{
    if (!exists) {
        return std::nullopt;
    }

    return std::string {"Alice"};
}
```

使用时：

```cpp
const auto result = find_name(true);

if (result.has_value()) {
    std::cout << result.value();
}
```

还可以写：

```cpp
if (result) {
    std::cout << result->size();
}
```

这里 `->` 看起来和指针相似，表示访问 `optional` 内部对象的成员。

C# 的可空值或返回 `null` 也能表达类似情况；`optional` 的优势是“可能没有结果”直接写进了函数类型。

### 为什么不返回一个假的数据报

如果空队列返回一个字段全是零的 `OutboundPacket`，调用者无法确定：

```text
这是真的零值数据，还是根本没有数据？
```

`nullopt` 把两种状态明确分开。

---

## 7. `enum class`：给几种结果起名字

Lab 中：

```cpp
enum class ReceiveResult {
    forwarded,
    ttl_expired,
    no_route,
    queue_full,
};
```

它表示 `receive` 的正常结果只能是这四种之一。

使用时写完整名字：

```cpp
ReceiveResult::forwarded
ReceiveResult::ttl_expired
```

这与 C# 枚举很接近：

```csharp
enum ReceiveResult
{
    Forwarded,
    TtlExpired,
    NoRoute,
    QueueFull
}
```

### 正常结果与异常不同

TTL 过期、没有路由和队列满，都是数据报网络中预期会发生的结果，所以用 `ReceiveResult` 返回。

调用者传入不存在的接口索引，则是错误使用接口，因此现有代码会：

```cpp
throw std::out_of_range("unknown interface");
```

可以先这样区分：

```text
网络模型中的正常分支 → enum class
程序员违反函数使用条件 → exception
```

---

## 8. 按值传参：`receive` 得到自己的数据报

签名：

```cpp
ReceiveResult receive(Datagram datagram);
```

这里没有 `&`，所以从理解模型的角度，可以先认为 `receive` 得到了自己的 `Datagram` 值。

因此函数内部修改：

```cpp
--datagram.ttl;
```

修改的是函数准备转发的那一份，不会偷偷修改调用者原来的变量。

这非常适合当前模型：

```text
输入一份数据报
检查能否转发
成功时修改自己的 TTL
把修改后的数据报放入出口队列
```

### `std::move` 先知道到这个程度

你会看到：

```cpp
std::move(payload)
```

或：

```cpp
std::move(from_r1->datagram)
```

对当前 Lab，可以先理解为：

> 这个对象后面不再需要原内容，允许把它内部拥有的资源转交给新位置，减少昂贵复制。

`std::move` 本身不是网络动作，也不会把数据发送出去。它只是 C++ 对象所有权和性能相关的工具。

被移动后的对象仍然可以析构或重新赋值，但不要依赖它还保存原内容。测试代码移动 `from_r1->datagram` 后，也不再读取原数据报。

如果这一段暂时仍然模糊，不影响先做 Lab。你可以先把它读成“把这份数据交过去”。

---

## 9. 构造函数初始化列表：只需看懂结果

`Router` 构造函数：

```cpp
Router::Router(std::vector<std::size_t> interface_capacities)
    : interface_capacities_(std::move(interface_capacities))
    , queues_(interface_capacities_.size())
{
    if (interface_capacities_.empty()) {
        throw std::invalid_argument("a router needs at least one interface");
    }
}
```

冒号后面叫成员初始化列表。

对 Lab 行为，只需提取三个结果：

```text
1. 传入的容量列表保存到 interface_capacities_
2. queues_ 创建同样数量的空队列
3. 一个接口都没有时抛出异常
```

例如：

```cpp
Router router({2, 5});
```

构造完成后，可以想象成：

```text
interface_capacities_ = [2, 5]
queues_ = [[], []]
```

不需要修改构造函数。

---

## 10. 怎样阅读一个测试：准备、执行、检查

测试函数看起来很长，但大多数都可以拆成三段。

以 TTL 测试为例：

```cpp
void test_ttl_expiration()
{
    const auto host_a = ip(10, 0, 0, 1);
    const auto host_b = ip(10, 0, 0, 2);

    Router router({2});
    router.add_route(Route {host_b, host_b, 0});

    const auto result = router.receive(
        make_datagram(host_a, host_b, 1, "expires")
    );

    expect(result == ReceiveResult::ttl_expired, "TTL=1 must expire");
    expect(router.queued_count(0) == 0, "expired packet must not be queued");
}
```

### 第一步：准备

```text
A = 10.0.0.1
B = 10.0.0.2
路由器有一个容量为 2 的出口
到 B 的数据应该从接口 0 发给 B
```

### 第二步：执行

```text
让路由器接收一份 A → B、TTL=1 的数据报
```

### 第三步：检查

```text
结果必须是 ttl_expired
接口 0 队列仍必须为空
```

`expect(condition, message)` 可以暂时当作：

```text
如果 condition 不成立，测试失败并显示 message
```

### 读测试时先忽略什么

下面这些暂时不用深究：

```cpp
std::pair<const char*, std::function<void()>>
for (const auto& [name, test] : tests)
try / catch
```

它们只是负责依次运行测试并打印 `[PASS]` 或 `[FAIL]`，不是路由器逻辑。

### 一次只读一个测试

建议顺序：

```text
TTL expiration
No matching route
Successful forwarding
FIFO queue and tail drop
Two-router path
```

每读一个，只回答两件事：

1. 它给了路由器什么输入？
2. 它要求哪些状态保持或改变？

---

## 11. 三个 TODO 的签名到底承诺什么

这里不写实现，只翻译接口。

### `find_route`

```cpp
const Route* Router::find_route(
    IPv4Address destination
) const;
```

翻译：

> 给我一个最终目标地址；如果路由表中有匹配规则，给我一个指向那条只读规则的指针，否则告诉我没有。

它可以读取：

```text
routes_
```

它不应该修改：

```text
路由表、队列、数据报
```

### `receive`

```cpp
ReceiveResult Router::receive(Datagram datagram);
```

翻译：

> 路由器收到一份自己的数据报值；根据 TTL、路由和队列状态，返回四种结果之一，成功时还会改变相应出口队列。

它可能读取：

```text
routes_、interface_capacities_、queues_
```

它可能修改：

```text
自己的 datagram 副本、某个出口队列
```

### `transmit_one`

```cpp
std::optional<OutboundPacket> Router::transmit_one(
    std::size_t interface_index
);
```

翻译：

> 给我一个接口索引；如果该出口队列有数据，取出队首并返回，否则明确返回没有数据。

注意“取出”通常同时包含：

```text
得到队首元素
让队首元素离开队列
```

---

## 12. 一个不会泄露答案的容器练习

先不要运行，预测输出：

```cpp
#include <deque>
#include <iostream>
#include <string>
#include <vector>

int main()
{
    std::vector<std::deque<std::string>> queues(2);

    queues[0].push_back("D1");
    queues[0].push_back("D2");
    queues[1].push_back("D3");

    std::cout << queues[0].front() << '\n';
    queues[0].pop_front();
    std::cout << queues[0].front() << '\n';
    std::cout << queues[1].size() << '\n';
}
```

<details>
<summary>预测后再展开</summary>

输出：

```text
D1
D2
1
```

`queues(2)` 创建两个空队列。接口 0 按 FIFO 顺序保存 D1、D2；删除 D1 后，D2 成为队首。接口 1 中只有 D3。

</details>

---

## 13. 编译错误怎样看，才不会被一大屏吓住

C++ 编译器常常因为一个错误打印许多后续错误。

建议按这个顺序：

```text
1. 找第一个 error，而不是最后一个
2. 看文件名和行号
3. 判断它是语法错误、类型错误还是链接错误
4. 修完第一个后重新编译
```

几个常见现象：

| 错误现象 | 先检查什么 |
| --- | --- |
| `expected ';'` | 前一行或结构定义末尾是否漏分号 |
| `use of undeclared identifier` | 变量名是否拼错、是否超出作用域 |
| `no member named ...` | 成员名和对象类型是否正确 |
| `cannot convert ...` | 返回值或参数类型是否匹配 |
| `unresolved external symbol` | 函数是否只有声明、没有对应定义 |

如果卡住，发给我：

```text
第一个 error
对应代码前后约 10 行
你认为这段代码本来想做什么
```

这比粘贴几百行输出更容易定位。

---

## 14. Lab 开始时的推荐阅读顺序

不要从第一行顺序啃到最后一行。按下面路线：

```text
1. tiny_net.hh：只看 Datagram、Route、OutboundPacket
2. tiny_net.hh：看 Router 的公开函数和三个成员变量
3. tiny_net.cc：跳过 IPv4Address 已完成代码
4. 先理解 find_route 的输入和输出
5. 只读与 find_route 相关的测试
6. 再理解 receive 的四种结果
7. 最后处理 transmit_one 与 FIFO 队列
8. 两台路由器测试留到前三部分能工作之后
```

阅读时可以自己写三行中文伪代码，但先不要追求正确 C++ 语法：

```text
输入是什么？
成功时状态怎样变化？
失败时必须保持什么不变？
```

这三问比“我要怎么写代码”更早，也更重要。

---

## 知识锚点

- `vector<T>` 表示连续存放、可增长的一组 `T`；`deque<T>` 支持高效地从队首和队尾操作。
- `const T*` 可以指向一个只读 `T`，也可以用 `nullptr` 表示没有找到。
- `optional<T>` 把“有一个 T”与“没有 T”明确写进返回类型。
- 按值接收 `Datagram` 让函数处理自己的值；`std::move` 允许把不再需要的资源转交到新位置。
- 测试的核心是准备输入、执行行为、检查不变量，外层运行框架可以暂时忽略。

---

## 小结

1. 你不需要读懂 Lab 中所有 C++；IPv4 位运算、测试框架和 CMake 可以暂时当黑盒。
2. `vector<Route>` 是路由表，`vector<deque<OutboundPacket>>` 是多个 FIFO 出口队列。
3. `const Route*` 用指针或 `nullptr` 表达查找结果；`optional<OutboundPacket>` 表达队列可能为空。
4. `ReceiveResult` 给四种正常网络结果命名，错误接口索引才通过异常报告。
5. 先把函数签名和测试翻译成中文，再写 C++，会比直接盯着 TODO 更轻松。

---

## 检查站

不用运行代码，请用自己的话回答：

1. `Router router({2, 5});` 创建了几个出口？两个出口的队列容量分别是多少？

2. 从里面向外解释 `std::vector<std::deque<OutboundPacket>> queues_`。`queues_[1]` 表示什么？

3. `Route {host_b, next_hop, 0}` 中三个值分别进入哪个字段？

4. `const Route*` 为什么适合表示路由查找结果？找到和没找到分别怎样表示？

5. `std::optional<OutboundPacket>` 的“有值”和“无值”在 `transmit_one` 中分别意味着什么？

6. 为什么 `receive(Datagram datagram)` 内部减少 `datagram.ttl`，通常不会修改调用者原来的变量？

7. 队列当前为 `[D1, D2]`。依次执行 `front()`、`pop_front()` 后，取得了谁，队列还剩什么？

8. 下面四部分中，哪些是明天开始 Lab 前必须大致看懂的，哪些可以暂时略过？请说明理由。

   - `Datagram`、`Route` 的字段
   - `IPv4Address::from_octets` 的位运算
   - 三个 TODO 的函数签名
   - 测试程序 `main` 中的 `std::function`

如果这 8 题大部分能用自己的话回答，你的 C++ 已经足够开始 Lab 0。真正写代码时遇到的具体语法，我们再按需要补，不必先恢复整门 C++。
