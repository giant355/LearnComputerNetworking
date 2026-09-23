# 第 24 课：TCP 只给字节流，消息边界由谁恢复？

> 预计时间：约 1.5～2 小时
> 场景：先读机制，再看短 C# 代码；不需要打开 Unity
> 前置：[[10_端口与UDP|UDP 数据报与 TCP 字节流]]、[[11_有限容量字节流|有限容量字节流]]、[[23_Unity消息_事件状态与可靠性|游戏消息的命令、事件、状态与时效]]

[打开交互：消息被切开和合并时怎样拆帧](24_TCP消息边界_交互.html)

## 一个很自然、但会出错的假设

游戏代码连续发送两条消息：

```csharp
await stream.WriteAsync(loginBytes, 0, loginBytes.Length);
await stream.WriteAsync(moveBytes, 0, moveBytes.Length);
```

你可能会想：接收端调用两次 `ReadAsync`，第一次拿到登录消息，第二次拿到移动消息。

但 TCP 不保存“发送方调用了两次 Write”这个边界。它交付的是有序字节流；一次 `ReadAsync` 可能只拿到半条消息，也可能一次拿到一条半，甚至同时拿到好几条。接收代码必须允许这些情况：

```text
发送方写： [登录消息] [移动消息]
接收方读： [登录的一部分] [登录剩余部分 + 移动的一部分] [移动剩余部分]
```

这不是数据被改乱了，而是**字节顺序有保证，Read 的分块边界没有消息含义**。第 10、11 课已经建立了这条基础；本课要补上的问题是：应用怎样在字节流上重新找回消息边界？

## 1. 序列化与拆帧解决不同问题

假设游戏里有一个 C# 对象：

```csharp
new ChatMessage { PlayerId = 7, Text = "你好" }
```

网络不能直接传这个进程里的对象引用。发送端先把它**序列化**成字节，例如 JSON 的 UTF-8 字节。接收端还需要知道：这些字节里，哪一段属于一条完整消息？这一步叫**消息分帧**（framing），也可以通俗地理解为“拆出一条一条应用消息”。

```text
C# 对象
  ↓ 序列化：对象变成字节
消息内容 bytes
  ↓ 分帧：给内容加上可识别的边界
应用层消息帧
  ↓ TCP
接收端字节片段
  ↓ 分帧：等齐一帧并取出内容
消息内容 bytes
  ↓ 反序列化
C# 对象
```

**序列化回答“对象怎样表示成字节”；分帧回答“字节流里一条消息到哪里结束”。** 即使使用 JSON、Protobuf 或自定义二进制格式，TCP 上仍然需要某种边界规则。

## 2. 三种找消息边界的朴素办法

### 办法 A：假设一次 Read 就是一条消息

不成立。Read 只是“现在拿到一些可用字节”，它不知道发送方原来怎样调用 Write。登录消息可能被分成多次 Read；连续的两条消息也可能合在一次 Read。

### 办法 B：每条消息都固定长度

如果所有消息刚好都是 64 字节，接收端每收满 64 字节就切一条，确实容易分帧。但短消息会浪费空间，长消息又放不下；不同消息长度变化时，固定长度不方便。

### 办法 C：遇到特殊结束符就切开

例如规定每条文本消息以换行符结束：

```text
登录|玩家7\n移动|玩家7|右\n
```

这适合有明确文本分隔规则的协议。但如果正文自己也能包含换行符，就得再规定转义方式；遇到二进制内容也不一定方便。

更通用的一种办法是：**先告诉接收端正文有多长，再发送正文。**

## 3. 长度前缀：先读长度，再等正文

我们先设计一个最小协议：

```text
帧 = 4 字节的正文长度 + 正文
长度只计算正文，不包括这 4 个长度字节
长度整数按大端序（big-endian）写入
```

四字节长度头是我们的应用协议选择，不是 TCP 规定的格式。大端序表示先写较高位：数字 3 写成 `00 00 00 03`。只要两端一致，也可以另选字节顺序；协议必须明确写出来，不能让双方各猜各的。

假设正文是三个字节 `ABC`，另一个正文是两个字节 `DE`：

```text
第一帧：[00 00 00 03] [41 42 43]
第二帧：[00 00 00 02] [44 45]
         └ 长度为 3 ┘  └ ABC ┘
```

这里 `41 42 43` 是 `A B C` 的十六进制字节。两帧在 TCP 字节流中顺序连接：

```text
00 00 00 03 41 42 43 00 00 00 02 44 45
```

接收端按协议工作：

1. 暂存收到的字节；不足 4 字节，说明长度字段还没到齐，继续等。
2. 读出前 4 字节，得到正文长度 `N`。
3. 暂存区里正文不足 `N` 字节，继续等；不要把半条消息交给反序列化器。
4. 正文到齐，取出一帧，把剩下的字节继续按同一规则解析。

关键不变量是：**没有收齐“长度字段 + 指定长度的正文”之前，不消费这条帧；收齐后恰好消费这些字节。**

## 4. 手动追踪一次拆帧

假设 TCP 三次提供给应用的字节片段是：

```text
Read 1：00 00
Read 2：00 03 41 42
Read 3：43 00 00 00 02 44 45
```

`Read 1` 只有两个字节，长度字段需要四个，所以暂存 `00 00`。`Read 2` 到来后，暂存区变为：

```text
00 00 00 03 41 42
```

现在能读到正文长度是 3，但正文只有 `41 42` 两个字节。继续等，不能提前交付 `AB`。

`Read 3` 到来后，第一帧的最后一个正文字节 `43` 到了；它后面还紧跟着第二帧的完整长度和正文。解析器应当连续取出：

```text
第一条正文：41 42 43 → ABC
第二条正文：44 45    → DE
最后剩余：空
```

因此解析器不能只“每次有新字节就试一次”。它需要循环：**只要暂存区里还够拼出一帧，就继续拆；不够下一帧时停下来保留残余字节。**

配套交互可以逐段推进同一组字节，观察“新到的片段”“暂存区”和“已拆出的消息”怎样变化。

## 5. 看懂一个 C# 长度前缀

发送端可以这样把正文包进帧：

```csharp
using System;
using System.IO;

public static class FrameEncoder
{
    public const int HeaderSize = 4;
    public const int MaxPayload = 64 * 1024;

    public static byte[] MakeFrame(byte[] payload)
    {
        if (payload.Length > MaxPayload) throw new InvalidDataException("消息太长");
        var frame = new byte[HeaderSize + payload.Length];
        int length = payload.Length;
        frame[0] = (byte)(length >> 24); frame[1] = (byte)(length >> 16);
        frame[2] = (byte)(length >> 8); frame[3] = (byte)length;
        Buffer.BlockCopy(payload, 0, frame, HeaderSize, payload.Length);
        return frame;
    }
}
```

读取长度时需要把四个字节按同样的大端序合起来：

```csharp
int length = (buffer[0] << 24) | (buffer[1] << 16) | (buffer[2] << 8) | buffer[3];
```

`buffer` 是解析器已经暂存的字节集合。每一项先提升成整数，再移到对应位置，用按位或合并。发送方和接收方必须约定同一种字节顺序；否则同一组字节会被解释成不同长度。

接收端的核心流程可以写成下面这样。这里用 `List<byte>` 是为了看清状态变化，后面项目若需要更高性能再换环形缓冲区等结构：

```csharp
using System;
using System.Collections.Generic;
using System.IO;

public sealed class FrameParser
{
    private const int HeaderSize = 4;
    private const int MaxPayload = 64 * 1024;
    private readonly List<byte> buffer = new List<byte>();
    public Action<byte[]> MessageReceived;

    public void OnBytes(byte[] chunk, int count)
    {
        for (int i = 0; i < count; i++) buffer.Add(chunk[i]);
        while (buffer.Count >= HeaderSize)
        {
            int length = (buffer[0] << 24) | (buffer[1] << 16) | (buffer[2] << 8) | buffer[3];
            if (length < 0 || length > MaxPayload) throw new InvalidDataException("非法消息长度");
            if (buffer.Count < HeaderSize + length) break;
            byte[] payload = buffer.GetRange(HeaderSize, length).ToArray();
            buffer.RemoveRange(0, HeaderSize + length);
            MessageReceived?.Invoke(payload);
        }
    }
}
```

`MessageReceived` 是解析出完整正文后交给上层的回调。此处只拆帧，还没有把正文反序列化成具体的 `ChatMessage`。这个区分能让“字节边界错了”和“JSON/二进制字段解析错了”成为两类可单独定位的问题。

这里的 `Action<byte[]>` 是“接收一组字节的函数”这个类型；`MessageReceived?.Invoke(payload)` 表示：如果上层已经登记了处理函数，就把完整正文交给它。

实际调用方每次读取都要传入有效字节数 `count`，不能假定读缓冲区整块都有效：

```csharp
int count = await stream.ReadAsync(readBuffer, 0, readBuffer.Length);
if (count == 0) return; // 对端关闭了发送方向，不是一条空消息
parser.OnBytes(readBuffer, count);
```

`ReadAsync` 可能只读到一小段，所以解析器要长期保留未完成帧的前缀或正文。`count == 0` 在 TCP 流中表示对端有序关闭，不要把它误当作“正文长度为 0”。

### 代码现在只需读懂什么？

- **必须读懂**：每次只追加 `count` 个字节；凑不齐头或正文就等；够一帧才移除；`while` 允许一次拆出多帧。
- **暂时可视为普通容器操作**：`List<byte>.GetRange`、`ToArray`、`RemoveRange`。它们是这份教学版解析器的实现选择，不是协议规则。

## 6. 长度数的是字节，不是字符

如果正文先用 UTF-8 编码，就必须对**编码后的字节数组**取长度：

```csharp
using System.Text;

byte[] payload = Encoding.UTF8.GetBytes("你好");
int bodyLength = payload.Length; // 6 个 UTF-8 字节
byte[] frame = FrameEncoder.MakeFrame(payload);
```

在 C# 中，`"你好".Length` 是 2，而这两个汉字的 UTF-8 编码一共是 6 字节。长度前缀要写 6，因为网络上传的是这 6 个字节，不是 C# 字符数量。

## 7. 为什么必须限制长度

长度字段来自网络，不能因为它写着“正文有几 GB”就立刻申请几 GB 内存。一个恶意或损坏的长度字段可能让接收端浪费内存。解析器应先验证长度在协议允许范围内，再等待正文：

```text
0 ≤ length ≤ MaxPayload
```

本课的例子将 `MaxPayload` 设为 64 KiB，只是教学协议的约定，不是 TCP 的固定限制。真实协议要根据消息类型、带宽和内存预算定义上限；超过上限通常应报协议错误并关闭连接。大文件应设计成多个有界块，而不是一次声明一个无穷大的消息。

长度前缀只解决“边界在哪里”，**不**解决消息内容是否合法、发送者有没有权限、请求是否过期、消息丢失后是否重传。这些仍由序列化格式、游戏规则和传输策略分别处理。

## 8. 和 UDP 数据报对照

TCP 是连续字节流，所以两条应用消息之间的边界要由应用协议重新定义。UDP 则以数据报为单位：接收端知道一次收到的是哪个数据报，但如果一条逻辑消息被应用拆成多个数据报，或一个数据报里装了多条逻辑消息，应用仍需定义自己的组织规则。

所以不是“UDP 不需要协议、TCP 才需要协议”；而是**两者提供的传输单位不同**。长度前缀适合把多条变长消息连续放进 TCP 字节流。

## 常见混淆

### “一次 Write 对应一次 Read”

不保证。发送调用的边界不是 TCP 字节流里的消息边界。

### “长度前缀就是 TCP 首部”

不是。它是应用协议自行定义的字节，位于 TCP 载荷之中；TCP 不知道它代表消息长度。

### “凑齐长度字段就能交给应用”

不行。长度字段只告诉解析器还要等多少正文；正文也完整到齐后，才有一帧。

### “反序列化器能自动发现边界”

通常不能。反序列化器应拿到一条完整的正文；分帧器先负责从 TCP 字节流中取出正文。

## 本课小结

- TCP 保留字节顺序，但不保留应用的 `Write` / `Read` 调用边界。
- 序列化把对象变成字节；分帧把字节流切回一条条完整消息。
- 长度前缀的接收端要先收齐固定长度的头，再按长度等待正文；完整后循环拆帧。
- 长度按字节计算，并限制最大值；拆帧错误与正文反序列化错误是不同问题。
- UDP 提供数据报单位，TCP 提供字节流单位；应用消息的设计仍需双方约定。

## 检查站

先遮住正文，用自己的话或手算回答。代码题看状态变化即可，不要求背 API。

1. 发送端连续两次写入 `ABC` 和 `DE`。接收端为什么可能先读到 `AB`，下一次再读到 `CDE`？
2. 本课约定帧格式为“4 字节大端正文长度 + 正文”，长度字段是否把这 4 个自身字节算进去？
3. 正文有 600 字节，完整帧总长度是多少？
4. C# 字符串 `"你好"` 的 `Length` 是 2；若正文按 UTF-8 编码，长度前缀应写多少？为什么？
5. 解析器先后收到：
   ```text
   00 00
   00 03 41 42
   43 00 00 00 02 44 45
   ```
   每次之后为什么要等或交付？最终交付哪些正文？
6. 为什么解析完整帧后要继续用 `while`，而不是只判断一次？
7. 长度字段声称正文有 2 GiB，而协议上限是 64 KiB。解析器应在什么时候拒绝？为什么不能先照这个长度分配内存？
8. `ReadAsync` 返回 `0` 与收到一个长度为 `0` 的应用消息，是不是一回事？
9. 这套长度前缀由 TCP 负责解释，还是由应用层解析器负责解释？

## 下一步

先完成检查站。下一课 [[25_应用消息包络与分派|第 25 课：消息包络、分派与请求关联]] 会在完整帧之上增加版本、类型和请求 ID，并说明它们怎样把消息送到正确处理路径；之后再从零做一段可运行的 C# 通信，而不是先背框架 API。
