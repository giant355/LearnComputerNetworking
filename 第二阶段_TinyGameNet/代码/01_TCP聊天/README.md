# TCP 客户端与服务器：当前代码存档

存档日期：2026-10-07。

- [Unity 客户端 CubeClient.cs](Client/CubeClient.cs)：复制自 `D:\unityGame\My project (15)\Assets\Scripts\CubeClient.cs`，是本次纠正后的实际客户端文件。
- [服务器 Program.cs](Server/Program.cs)：逐字复制自 `E:\ConsoleApp1\ConsoleApp1\Program.cs`。
- [服务器消息定义](Server/NetMessage.cs)：复制自同一服务器工程。
- [服务器工程文件](Server/ConsoleApp1.csproj)：复制自同一服务器工程，使用 .NET 10，可直接运行。

## 当前做到哪里

客户端后台建连、发送队列、持续接收、主线程处理；服务器接受多个客户端，为每条连接建立接收线程。聊天消息回给原客户端，并追加 `,服务端已收到`。`MoveInput` 类型目前仅打印，尚未实现聊天广播、玩家身份通知或 Cube 位置同步。

## 双方的消息格式

```text
[4 字节大端正文长度][1 字节消息类型][UTF-8 内容]
```

正文长度包含类型字节，不含 4 字节长度前缀。正文上限为 64 KiB。类型 `1` 为聊天，`2` 为移动输入。先收满前缀，再按长度收满正文，不依赖一次 Receive 恰好返回整条消息。

## 运行

安装 .NET 10 SDK 后，在本目录打开 PowerShell：

```powershell
dotnet run --project .\Server\ConsoleApp1.csproj
```

服务器当前固定监听本机 `127.0.0.1:8080`。如果端口被占用，先关闭重复启动的服务器；需要更换端口时，修改服务端 Bind 的端口，并让客户端配置与之相同。

将 `Client/CubeClient.cs` 放入 Unity 2022.3 项目，在场景空物体上挂载 `CubeClient`，Inspector 的 `Server Port` 与服务器一致。此文件包含 `MessageType` 和 `NetMessage`，同一个 Unity 项目不要重复定义这两个类型。

启动服务器，再运行 Unity。连接后自动发送 `hello`；点击 Game 窗口后，按空格持续发送带编号的消息，回复显示在 Console。客户端地址当前固定为回环地址，只用于同一电脑上的测试。按回车关闭服务器，退出 Play 时客户端关闭连接。

## 验证范围

客户端修正时已通过独立 C# 9 编译，以及本机 Socket 的中文、分批接收、连续消息、断开、非法长度和截断正文检查。Unity 画面和实际 Play 行为仍需在编辑器里运行观察。

本次存档另外编译了实际服务器工程（0 警告、0 错误），并使用临时本机端口运行其实际 `ClientSocket.ReceiveLoop` 与两个客户端的后台收发逻辑，确认聊天协议一致、中文与连续消息回显正确、分批前缀和正文能还原、非法长度不影响其他连接、关闭 Socket 后接收循环退出。该验证使用 Unity API 替身，不代表 Unity 场景或 Cube 同步已测试。
