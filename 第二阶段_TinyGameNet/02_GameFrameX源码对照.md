# 带着自己的实现读 GameFrameX

返回：[项目设计与学习路线](00_项目设计与学习路线.md) · [通信设计](01_通信与代码设计.md)

## 1. 这次参考的范围

参考的是 [GameFrameX 官方 Unity 网络模块](https://github.com/GameFrameX/com.gameframex.unity.network)，本地源码包的 `package.json` 标注版本 `2.6.10`，本次设计已查看它的通道、Socket 收发、消息包头和请求等待实现。

本地源码位置：`C:\Users\Lenovo\Documents\Codex\2026-08-31\d-d-d-d-d\work\GameFrameX-source\com.gameframex.unity.network-main`。

上面是源码快照，不是完整 GameFrameX Unity 工程。服务端尚未在本项目里下载、运行或验证；Actor、多房间等安排是后续研究方向。

我们会借鉴模块解决问题的方法。当前设计不会直接与 GameFrameX 服务端互通；协议字段、消息编号、序列化配置都需要明确一致，使用同一种 TCP 或 Protobuf 本身不够。

## 2. 在什么时候打开哪个文件

下表路径相对于该网络模块根目录。每次先回顾我们已经做出的效果，再看对应源码。

| 我们刚解决的问题 | GameFrameX 文件 | 阅读时只追这件事 |
| --- | --- | --- |
| 创建对象和建立连接不同 | `Runtime/Network/NetworkComponent.cs`、`Runtime/Network/Network/NetworkManager.cs` | CreateNetworkChannel 创建、登记了什么，Connect 又在哪里发生 |
| 连接并等待收发完成 | `Runtime/Network/Network/SystemSocket/NetworkManager.SystemTcpNetworkChannel.cs` | 从 Connect 到 BeginConnect；从 BeginReceive/EndReceive 得到读取数量 |
| 一次只收到半条消息 | `Runtime/Network/Network/NetworkManager.ReceiveState.cs` 及上面的 TCP 通道文件 | 当前收的是包头还是正文，还差多少字节 |
| 给业务内容加包头 | `Runtime/Network/Helper/DefaultPacketSendHeaderHandler.cs`、`Runtime/Network/Helper/DefaultPacketReceiveHeaderHandler.cs` | 长度统计哪部分，消息编号和唯一编号分别做什么 |
| 对象与字节互转 | `Runtime/Network/Interface/IMessageSerializer.cs`、`Runtime/Network/Network/MessageSerializerRegistry.cs` | 为什么序列化器可以替换，谁选择实际实现 |
| Send 调用后消息先排队 | `Runtime/Network/Network/NetworkManager.NetworkChannelBase.cs` | Send、ProcessSend、ProcessSendMessage 各推进哪一步 |
| 找回某次请求的响应 | `Runtime/Network/Network/NetworkManager.RpcState.cs` | 等待表如何登记、匹配、超时和清理 |

表中第二行使用 `Begin.../End...` 形式的异步回调；我们的第一版使用 `Task/await`。两者都要表达“操作可能稍后完成”，但编排方式不同，第一次对照时会单独解释。

## 3. 一个现在就能看懂的实际差异

我们暂定：

```text
bodyLength = 包络的 8 字节 + payload 长度
整个应用帧长度 = 4 + bodyLength
```

本次检查到 GameFrameX 的 `DefaultPacketSendHeaderHandler` 使用：

```csharp
PacketLength = (uint)(PacketHeaderLength + messageLength);
```

它的固定包头当前共 14 字节，包含长度、操作标记、压缩标记、唯一编号和消息 ID。它计算的是连包头一起的总长度，接收端减去包头长度才得到正文长度。

这是两种不同约定，都可以正确工作。读源码时必须查发送方怎么写、接收方怎么算，不能看到“长度”两个字就套用旧公式。我们会保留清楚的自有协议，再在需要时讨论兼容迁移。

## 4. 第一版之后怎样靠近

先从自己已经使用的 `TcpConnection`、`FrameCodec` 和业务分派找重复点，再决定是否抽象成 Channel、序列化接口和通用请求调用。

引入接口时必须有具体理由，例如第二种序列化器、第二种传输或测试替身。遇到源码里的反射、对象池、热更新相关机制时，先标出它与当前消息路径的关系，再决定是否值得深入。

每一轮只回答三件事：它在解决什么问题？我们在哪里遇到了同一问题？它多出来的复杂度换来了什么？

## 5. 下一步阅读的边界

当前已看过 CreateNetworkChannel。继续深入源码前，先在自己的项目完成 M0、M1，让“通道、连接、消息”分别对应看得见的对象和运行现象。之后每做出一小块，就回到本表读相应实现。
