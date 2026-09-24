# 第 26 课：C# 怎样接上 TCP？——Socket、TcpClient、TcpListener 与 NetworkStream

> 预计时间：约 3.5～4 小时，可拆成两次学习
> 环境：先用两个普通 C# 控制台程序观察；不需要 Unity，也不需要网络框架
> 前置：[[24_TCP消息边界与长度前缀|第 24 课：TCP 字节流与长度前缀]]、[[25_应用消息包络与分派|第 25 课：消息包络、分派与请求关联]]

[打开交互：从监听端口到两端字节流](26_TCP_API_交互.html)

## 本课补上哪一段

第 24、25 课已经能描述一条游戏消息怎样变成字节、怎样切出完整帧、怎样分派给处理器。但这些仍是应用程序里的模型。现在轮到一个很实际的问题：

> C# 程序调用哪个对象，才能请操作系统建立 TCP 连接，并把字节送进网络？

先把整个路径放在眼前：

```text
游戏逻辑 / 普通 C# 代码
    ↓ 生成协议字节
TcpClient 或 TcpListener
    ↓ 使用
Socket（操作系统提供的网络端点句柄）
    ↓ 操作系统 TCP/IP 协议栈
IP → 链路 → 网络 → 对端操作系统
    ↓
对端 Socket → NetworkStream → C# 代码
```

这里没有新的传输协议：`TcpClient` 和 `TcpListener` 是调用 TCP 的 C# 入口；TCP 的连接、重传、排序仍由操作系统协议栈负责。Microsoft 文档也明确说明，这两个较易用的类建立在 `Socket` 之上，而 `NetworkStream` 提供面向流的读写接口。[.NET TCP 类说明](https://learn.microsoft.com/en-us/dotnet/fundamentals/networking/sockets/tcp-classes)

## 1. 先预测：`WriteAsync` 写出去的是什么？

假设客户端已经用第 25 课的规则，把“购买药水”编码为一组字节。它执行：

```csharp
await stream.WriteAsync(bytes, 0, bytes.Length);
```

这行代码没有把 C# 的 `PurchaseRequest` 对象直接搬到服务器，也没有告诉 TCP “这是一个完整的游戏消息”。它把指定范围内的**字节**交给流对象；如何切分游戏消息，仍由第 24 课的长度前缀拆帧器负责。

预测一下：若先后调用 `WriteAsync(A)` 和 `WriteAsync(B)`，接收方的 `ReadAsync` 会不会必定先返回 A、再单独返回 B？

不会。TCP 保证的是字节流的顺序，不是 C# 写入调用的边界。一次读取可能只得到 A 的一部分，也可能拿到 A 的剩余部分加上 B 的开头。我们已经在第 24 课学过怎样从字节流恢复帧；本课要理解这些字节流对象从哪里来。

## 2. 四个名字，四种职责

| 名字 | 可以先把它理解成 | 它不负责什么 |
| --- | --- | --- |
| `Socket` | 操作系统网络端点的低层句柄；可创建、绑定地址和端口、连接、收发 | 不自动理解 `PurchaseRequest` 这样的游戏对象 |
| `TcpClient` | 客户端使用的 TCP 连接包装；调用 `ConnectAsync`，再取出流 | 不代表“玩家客户端对象”，也不负责业务授权 |
| `TcpListener` | 服务端在本机某个地址和端口等待连接的包装 | 不等于每个玩家的连接，也不是收发所有玩家消息的同一根流 |
| `NetworkStream` | 已建立连接上的双向字节流视图；提供 `Read` / `Write` 等操作 | 不保存应用层消息边界，不解析长度、类型、请求 ID |

可以把它们想成：`Socket` 是操作系统给的网络接口把手；`TcpClient` 与 `TcpListener` 是更符合“我要连接”“我要接客”这两种使用方式的包装；`NetworkStream` 则让连接能用 C# 的流读写方式传字节。

**Socket 不是 TCP 本身。**Socket 是程序可以调用的接口对象；TCP 是操作系统实际执行的协议机制。多个语言（C#、C++ 等）都能调用操作系统提供的 Socket 接口。

## 3. 服务器先准备“有人来敲门”的端点

我们在本机做实验，用 `127.0.0.1`（也可由 `IPAddress.Loopback` 表示）和端口 `5050`：

```csharp
var listener = new TcpListener(IPAddress.Loopback, 5050);
listener.Start();
```

这一刻程序没有创建玩家，也没有创建应用消息。它做的是：

1. 指定本机 IPv4 回环地址与端口 `5050`；
2. 调用 `Start()`，让底层 Socket 绑定这个本地端点并开始监听；
3. 之后操作系统收到发往这里的 TCP 连接请求时，能将它交给这个监听端点。

因为使用 `Loopback`，这次实验只允许本机程序连进来；同一局域网里的另一台电脑不能用这个地址访问它。实际部署时如何选网卡地址、开放防火墙和做公网连接，是后面的独立问题。

此时可以画成：

```text
服务器进程
└─ TcpListener：本机 127.0.0.1:5050，正在监听
```

**监听端点不是已建立连接。**它像一个服务入口，尚未与某个具体客户端形成一条可以读写的连接。

## 4. 客户端发起连接；操作系统补齐本地端点

客户端使用：

```csharp
var client = new TcpClient();
await client.ConnectAsync(IPAddress.Loopback, 5050);
```

这里的地址与端口是**服务器目标端点**。客户端通常不必自己挑本地源端口：若没有先手动绑定，操作系统会为连接选择可用的本地端口，并确定本机出接口地址。

在本机示例里，最终连接可能类似：

```text
客户端本地端点：127.0.0.1:53024
服务器端点：    127.0.0.1:5050
```

客户端和服务器各自都在本机维护 TCP 连接状态。TCP 三次握手、初始序号和重传状态属于前面课程的内容；此处关注的是 API 怎样让应用请求操作系统完成连接。

若把目标写成域名而不是 `IPAddress`，还会有 DNS 解析步骤；本课刻意用回环 IP，暂时不让 DNS 这条支线遮住 Socket 的职责。

## 5. `Accept` 为什么会返回另一个 `TcpClient`？

服务器的监听代码继续等待：

```csharp
TcpClient peer = await listener.AcceptTcpClientAsync();
```

这个调用完成后，`peer` 代表一条**具体已建立的连接**。它不是 `listener` 本身：

```text
服务器进程
├─ TcpListener：仍负责等待后续连接，目标端口 5050
└─ peer：本次被接受的连接，例：客户端 53024 ↔ 服务器 5050
```

一个监听端点可以持续接受许多客户端。每次 `AcceptTcpClientAsync()` 都会为一条新连接交出新的连接对象；监听端点仍继续存在。操作系统根据连接两端地址和端口区分这些已连接状态。Microsoft 的 TCP 类说明也指出，接受连接时底层会创建新的连接 Socket。[连接接受流程](https://learn.microsoft.com/en-us/dotnet/fundamentals/networking/sockets/tcp-classes)

**本课演示程序只接受一个连接**，是为了看清流程。真正的房间服务器还要循环接收，并为不同连接运行各自的处理流程；稍后的 Lab 会从整体结构开始设计多客户端版本。

## 6. `GetStream()` 得到的是这条连接上的字节流

连接建立后：

```csharp
NetworkStream stream = peer.GetStream();
```

客户端和服务器都各自拿到代表**同一条 TCP 连接**的流对象。它是双向的：双方都能在自己的流上读，也能写。

```text
客户端 NetworkStream  --客户端写入字节-->  服务器 NetworkStream
客户端 NetworkStream  <--服务器写入字节--  服务器 NetworkStream
```

“双向”不表示两个方向共享同一个字节顺序编号。每个方向都是独立的字节流；这与第 16 课的双向 TCP 字节序号模型一致。

`GetStream()` 只能在 TCP 客户端已经连接后调用；连接未建立或对象已关闭时会失败。[`TcpClient.GetStream`](https://learn.microsoft.com/en-us/dotnet/api/system.net.sockets.tcpclient.getstream)

## 7. `ReadAsync` 返回的是“本次读到几个字节”

典型接收代码：

```csharp
byte[] buffer = new byte[1024];
int count = await stream.ReadAsync(buffer, 0, buffer.Length);
```

- `buffer`：程序给本次读取准备的内存空间；
- `buffer.Length`：本次最多想接收多少字节；
- `count`：这一次实际读到的字节数。

`count` 可能小于 1024；即使发送方只写了一次 1024 字节，也不能据此假定这一读就一定得到 1024。对 `NetworkStream.ReadAsync` 来说，返回值是当前这次实际读入的字节数；返回 `0` 通常表示对端已优雅地关闭了它的发送方向，且当前已没有更多字节可读。[`NetworkStream.ReadAsync`](https://learn.microsoft.com/en-us/dotnet/api/system.net.sockets.networkstream.readasync?view=net-8.0)

正确心智模型是：

```text
ReadAsync(buffer, 最多 1024)
    → 等到有字节可读或流结束
    → 把当前可用的一部分放进 buffer
    → 返回 count
```

不能写成“调用一次 `ReadAsync` 就收到一条游戏消息”。更不能忽略 `count`，直接把整块 buffer 都反序列化：buffer 末尾未被本次读取覆盖的部分可能是旧数据或零值。

通常接收代码会反复读，并把每次得到的 `buffer[0..count)` 交给增量拆帧器：

```text
ReadAsync 得到任意一段字节
       ↓
追加到接收缓冲区
       ↓
第 24 课拆出 0、1 或多条完整帧
       ↓
第 25 课解析包络并分派
```

## 8. `WriteAsync` 完成，能证明什么？

发送：

```csharp
byte[] bytes = Encoding.UTF8.GetBytes("hello");
await stream.WriteAsync(bytes, 0, bytes.Length);
```

它表示这次流写入操作已完成且没有向当前代码抛出错误。**它不能证明**：

- 对端游戏程序已经运行到读取代码；
- 对端已经拆出完整应用帧；
- 购买请求已经通过业务验证；
- 服务器已经扣款并返回成功结果。

字节可能仍在本机操作系统的发送队列或网络路径中；可靠交付由 TCP 机制负责，而业务是否接受则由服务器处理器决定。后者必须等应用层的 `PurchaseResult`，不能拿 TCP ACK 或 `WriteAsync` 的完成代替。

## 9. 一个可运行的本机回显程序

这不是下一阶段的完整 Lab，只用来把 API 名字绑定到看得见的行为。服务端收完客户端发送方向的字节后，把同样的字节回送；客户端发送完后半关闭自己的发送方向，但仍等待接收服务端回显。

这次用“客户端发送方向结束”作为整批实验输入的边界，方便小程序结束。**真实游戏不会每发一条消息就关连接**；持续连接中的消息边界仍由第 24 课长度前缀处理。

### Server 程序

```csharp
using System;
using System.IO;
using System.Net;
using System.Net.Sockets;
using System.Text;
using System.Threading.Tasks;

class ServerProgram
{
    static async Task Main()
    {
        var listener = new TcpListener(IPAddress.Loopback, 5050);
        listener.Start();
        Console.WriteLine("等待本机客户端连接...");
        try
        {
            using (TcpClient peer = await listener.AcceptTcpClientAsync())
            {
                Console.WriteLine("已接受一条连接：" + peer.Client.RemoteEndPoint);
                NetworkStream stream = peer.GetStream();
                using (var received = new MemoryStream())
                {
                    byte[] buffer = new byte[256];
                    int count;
                    while ((count = await stream.ReadAsync(buffer, 0, buffer.Length)) > 0)
                    {
                        Console.WriteLine("本次 ReadAsync 得到 " + count + " 字节");
                        received.Write(buffer, 0, count);
                    }
                    byte[] reply = received.ToArray();
                    Console.WriteLine("累计收到：" + Encoding.UTF8.GetString(reply));
                    await stream.WriteAsync(reply, 0, reply.Length);
                    peer.Client.Shutdown(SocketShutdown.Send);
                }
            }
        }
        finally { listener.Stop(); }
    }
}
```

### Client 程序

```csharp
using System;
using System.IO;
using System.Net;
using System.Net.Sockets;
using System.Text;
using System.Threading.Tasks;

class ClientProgram
{
    static async Task Main()
    {
        using (var client = new TcpClient())
        {
            await client.ConnectAsync(IPAddress.Loopback, 5050);
            NetworkStream stream = client.GetStream();
            byte[] request = Encoding.UTF8.GetBytes("Hello from client!");
            await stream.WriteAsync(request, 0, request.Length);
            client.Client.Shutdown(SocketShutdown.Send);
            using (var reply = new MemoryStream())
            {
                byte[] buffer = new byte[256];
                int count;
                while ((count = await stream.ReadAsync(buffer, 0, buffer.Length)) > 0) { reply.Write(buffer, 0, count); }
                Console.WriteLine("服务器回显：" + Encoding.UTF8.GetString(reply.ToArray()));
            }
        }
    }
}
```

### 怎样观察

在任意已安装 .NET SDK 的电脑上创建两个普通控制台项目，分别放入 Server 与 Client 代码；先启动 Server，再启动 Client。两个 `Main` 的代码放在**不同项目**，因为同一个项目不能同时有两个入口点。

观察四件事：

1. Server 在 `Start()` 后停在 `AcceptTcpClientAsync()`，直到客户端来连；
2. 客户端连接后，Server 得到一个 `peer`，监听对象 `listener` 和连接对象 `peer` 不是同一个；
3. Server 的多次 `ReadAsync` 可能打印不同字节数，但 `MemoryStream` 最后累计的是收到的字节；
4. Client 执行 `Shutdown(SocketShutdown.Send)` 后，Server 的读取循环最终得到 `0`；Server 仍能在另一个方向回写，Client 收到回显后也读到结束。

如果本次小文本恰好一次读全，也不能据此推断“TCP 保留了写入边界”。那只是这一次调度和数据量下的观察结果；程序正确性不能依靠它。

## 10. `async` / `await` 为什么适合网络等待

如果改用同步的 `AcceptTcpClient()` 或 `Read()`，当前调用线程会停在那里等。控制台程序这样写尚能演示；Unity 主线程若同步等网络，就可能卡住画面、输入和 `Update()`。

`await` 的意义是：代码需要等异步操作完成，但不要求当前线程原地空转等待。它**不是**“自动开一个新线程”的同义词，也不改变 TCP 的传输规则。

一个最低限度的因果链：

```text
同步 Read：没数据 → 调用线程卡在 Read → Unity 这一帧不能继续
异步 ReadAsync：没数据 → 暂停当前异步流程 → 数据到来后继续执行
```

不要这样忙等：

```csharp
while (!stream.DataAvailable) { }
```

它会不断占用 CPU，且不能解决消息边界问题。应使用异步读取等待，再把实际读到的字节交给拆帧器。

## 11. 接到 Unity 时，要把网络工作与游戏对象操作分开

一个重要边界：`NetworkStream` 只给你字节，它不知道 `GameObject`、`Transform` 或 NGO 的 `NetworkObject` 是什么。把字节解成普通 C# 消息后，才轮到游戏逻辑。

```text
异步网络读取
    → 字节缓冲与拆帧
    → 普通 C# 消息对象
    → 交给游戏主线程的待处理队列
    → 在 Update / 合适的主线程阶段修改 Unity 世界
```

因此后续接入时会分出两个责任：

- **网络循环**负责连接、读写字节、拆帧和报告断开；
- **游戏逻辑**负责验证消息、更新权威状态、同步或显示对象。

若在异步回调或工作线程里随意改 Unity 对象，会遇到线程边界问题。此课不要求写 Unity 线程调度 API，只要记住：收到网络数据 ≠ 可以在任何线程直接修改场景。

Unity 2022.3 默认使用 .NET Standard 2.1 API 配置，但目标平台决定某些 API 是否真正可用；尤其 Unity WebGL 不能直接使用 `System.Net` 的原始 IP Socket，需要浏览器支持的 WebSocket/WebRTC 等方式。因此我们先用本机控制台程序学机制，后面会按你的 Unity 2022 目标平台检查接口可用性。[Unity 2022.3 .NET 配置](https://docs.unity3d.com/2022.3/Documentation/Manual/dotnetProfileSupport.html)；[Unity 2022.3 WebGL 联网限制](https://docs.unity3d.com/2022.3/Documentation/Manual/webgl-networking.html)

## 12. 常见误解，逐个拆开

### “`TcpClient` 就是游戏里的客户端玩家”

不是。它是一个进程中的 TCP 连接包装。一个客户端游戏进程可创建一个或多个 `TcpClient`；一个连接里又可传许多玩家业务消息。

### “服务器只有一个 Socket，所以一次只能有一个玩家”

监听 Socket 与每个已接受的连接 Socket 分工不同。监听对象等待新连接；每个接受对象对应一个连接。是否并发处理多个连接，取决于应用的接收循环和任务结构。

### “读到 80 字节，就收到了一条 80 字节消息”

不能这样推断。80 只是这次读出的字节数；拆帧器判断应用消息边界。

### “`WriteAsync` 返回了，服务器就执行成功了”

不成立。它只报告这次本地流写入操作的结果；业务成功要靠服务器处理并返回应用层响应。

### “异步操作就是后台线程”

不等价。异步描述调用流程如何等待完成；是否使用额外线程，要看具体运行时和操作，不要把两者混为一谈。

### “能在 Unity Editor 里跑就能在所有平台跑”

不一定。API 配置、脚本后端、目标平台和浏览器限制都可能不同。课程的控制台 Demo 是概念验证，不是跨平台兼容证明。

## 本课小结

- `TcpListener` 表示本机等待连接的服务入口；`AcceptTcpClientAsync()` 为每个已接受连接交出连接对象。
- `TcpClient` 通过 `ConnectAsync()` 请求连接远端；操作系统选择或使用本地端点并执行 TCP。
- `Socket` 是更底层的网络接口对象；`TcpClient` / `TcpListener` 是更方便的封装；`NetworkStream` 是连接上的双向字节流接口。
- `ReadAsync` 返回本次读到的字节数，不承诺完整应用消息；`WriteAsync` 也不承诺业务已经成功。
- 连接 API 解决的是“怎样连接和读写字节”；第 24、25 课的拆帧和分派解决的是“这些字节组成哪条应用消息、谁处理它”。
- Unity 代码要避免同步等待网络，并将普通消息处理与 Unity 场景对象操作放在清楚的执行边界上。

## 检查站

先遮住正文，按具体情境回答；不会术语时可以描述对象和动作。

1. Server 已调用 `listener.Start()`，但还没有客户端连入。此时 `listener` 代表什么？它是否已经是一条和玩家之间的连接？

2. `AcceptTcpClientAsync()` 返回了 `peer`。为什么 `peer` 不是 `listener` 的别名？若第二个玩家随后连入，服务器会怎样得到那条连接？

3. 两个玩家都连接同一服务器 IP 与端口 `5050`。服务器靠什么信息区分两条连接？客户端本地源端口通常由谁选择？

4. 客户端用一次 `WriteAsync` 发送 600 字节。服务端缓冲区有 1024 字节。能否保证下一次 `ReadAsync` 正好返回 600？为什么？

5. 接收端执行 `ReadAsync` 返回 `0`。这在流层面说明什么？是否必然意味着双方两个方向都已彻底关闭？

6. `await stream.WriteAsync(...)` 正常完成。列举一件它能证明的事和两件它不能证明的事。

7. 为什么不能把长度前缀拆帧逻辑放进 `TcpClient` 的概念定义里？它属于哪个组件的职责？

8. Unity 主线程直接调用同步 `Read()`，对帧率和输入可能造成什么影响？改成异步读取后，是否就可以从网络回调里任意修改 `Transform`？

9. 对照回显程序，从 `ConnectAsync` 到服务器 `ReadAsync` 得到字节，按顺序写出应用对象、C# API、操作系统和 TCP 各自承担的工作。

## 可选小观察

如果现在有电脑，可以先只运行本地回显程序，不必修改代码：

- 先启动 Client，没启动 Server，会看到什么失败？这说明客户端调用连接时依赖什么？
- 启动 Server 后再连一次，看看 `Accept` 什么时候返回；
- 多运行几次，看 `ReadAsync` 的 `count` 是否每次都一样。即使一直一样，也要解释为什么不能把这个现象当作协议保证。

不想做实验时，按代码逐句推演也能完成理论课。下一阶段会先由我们共同画出多连接服务器的数据流、缓冲区、待处理消息和关闭状态，再从空控制台项目实现，而不是直接给你一堆孤立 TODO。

## 参考资料

- Microsoft Learn：[Use TcpClient and TcpListener](https://learn.microsoft.com/en-us/dotnet/fundamentals/networking/sockets/tcp-classes)
- Microsoft Learn：[NetworkStream.ReadAsync](https://learn.microsoft.com/en-us/dotnet/api/system.net.sockets.networkstream.readasync?view=net-8.0)
- Unity Manual 2022.3：[.NET profile support](https://docs.unity3d.com/2022.3/Documentation/Manual/dotnetProfileSupport.html)
- Unity Manual 2022.3：[WebGL networking](https://docs.unity3d.com/2022.3/Documentation/Manual/webgl-networking.html)
