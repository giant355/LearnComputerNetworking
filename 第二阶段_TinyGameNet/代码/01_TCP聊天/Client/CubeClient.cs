using System;
using System.Collections.Concurrent;
using System.Net;
using System.Net.Sockets;
using System.Text;
using System.Threading;
using UnityEngine;

public enum MessageType : byte
{
    Chat = 1,
    MoveInput = 2
}

public sealed class NetMessage
{
    public MessageType Type;
    public string Content = string.Empty;
}

public sealed class CubeClient : MonoBehaviour
{
    private const int MaxBodyLength = 64 * 1024;
    [SerializeField] private int serverPort = 8080;

    private volatile bool running;
    private volatile bool connected;
    private Socket socket;
    private int chatNumber;

    private readonly ConcurrentQueue<string> logs = new ConcurrentQueue<string>();
    private readonly ConcurrentQueue<byte[]> sendQueue = new ConcurrentQueue<byte[]>();
    private readonly ConcurrentQueue<NetMessage> receiveQueue = new ConcurrentQueue<NetMessage>();

    private void Start()
    {
        socket = new Socket(AddressFamily.InterNetwork, SocketType.Stream, ProtocolType.Tcp);
        running = true;

        Thread connectThread = new Thread(ConnectToServer);
        connectThread.IsBackground = true;
        connectThread.Start();
    }

    private void ConnectToServer()
    {
        try
        {
            socket.Connect(new IPEndPoint(IPAddress.Loopback, serverPort));

            // 等待连接期间可能已经退出 Play；关闭后不再启动新的收发任务。
            if (!running)
            {
                return;
            }

            connected = true;
            logs.Enqueue("连接服务器成功");

            Thread sendThread = new Thread(SendLoop);
            sendThread.IsBackground = true;
            sendThread.Start();

            Thread receiveThread = new Thread(ReceiveLoop);
            receiveThread.IsBackground = true;
            receiveThread.Start();

            // 只入队一次，实际 Socket.Send 统一由发送线程执行。
            Send(new NetMessage { Type = MessageType.Chat, Content = "hello" });
        }
        catch (Exception error)
        {
            if (running)
            {
                logs.Enqueue("连接失败：" + error.Message);
            }

            StopNetworking();
        }
    }

    private void Update()
    {
        if (Input.GetKeyDown(KeyCode.Space))
        {
            chatNumber++;
            Send(new NetMessage { Type = MessageType.Chat, Content = "客户端消息" + chatNumber });
        }

        while (logs.TryDequeue(out string log))
        {
            Debug.Log(log);
        }

        // 完整消息在主线程处理；以后操作 UI、Cube 的代码也放在这里。
        while (receiveQueue.TryDequeue(out NetMessage message))
        {
            if (message.Type == MessageType.Chat)
            {
                Debug.Log("服务器：" + message.Content);
            }
            else if (message.Type == MessageType.MoveInput)
            {
                Debug.Log("收到移动输入：" + message.Content);
            }
        }
    }

    /// <summary>将消息编码并排队；本方法不直接调用 Socket.Send。</summary>
    /// <param name="message">要发送的消息。队列保存编码后的副本，不保存这个对象。</param>
    public void Send(NetMessage message)
    {
        if (message == null)
        {
            throw new ArgumentNullException(nameof(message));
        }

        if (!running || !connected)
        {
            logs.Enqueue("尚未连接，无法发送消息");
            return;
        }

        sendQueue.Enqueue(EncodeMessage(message));
    }

    private void SendLoop()
    {
        try
        {
            while (running)
            {
                if (!sendQueue.TryDequeue(out byte[] data))
                {
                    Thread.Sleep(10);
                    continue;
                }

                int offset = 0;

                while (running && offset < data.Length)
                {
                    int sent = socket.Send(data, offset, data.Length - offset, SocketFlags.None);

                    if (sent == 0)
                    {
                        throw new System.IO.IOException("发送未能继续。");
                    }

                    offset += sent;
                }
            }
        }
        catch (Exception error)
        {
            if (running)
            {
                logs.Enqueue("发送失败：" + error.Message);
            }

            StopNetworking();
        }
    }

    private void ReceiveLoop()
    {
        try
        {
            while (running)
            {
                NetMessage message = ReceiveNetMessage(socket);

                if (message == null)
                {
                    if (running)
                    {
                        logs.Enqueue("服务器已结束发送");
                    }

                    break;
                }

                receiveQueue.Enqueue(message);
            }
        }
        catch (Exception error)
        {
            if (running)
            {
                logs.Enqueue("接收失败：" + error.Message);
            }
        }
        finally
        {
            StopNetworking();
        }
    }

    private void StopNetworking()
    {
        connected = false;
        running = false;
        socket?.Close();
    }

    private void OnDestroy()
    {
        StopNetworking();
    }

    // 协议：[4 字节大端正文长度][1 字节类型][UTF-8 内容]。
    // 正文长度包含类型字节，但不包含长度前缀本身。
    static byte[] EncodeMessage(NetMessage message)
    {
        if (message.Type != MessageType.Chat && message.Type != MessageType.MoveInput)
        {
            throw new ArgumentOutOfRangeException(nameof(message), "未知的消息类型。");
        }

        byte[] content = Encoding.UTF8.GetBytes(message.Content ?? string.Empty);
        int bodyLength = 1 + content.Length;

        if (bodyLength > MaxBodyLength)
        {
            throw new ArgumentOutOfRangeException(nameof(message), "消息正文不能超过 64 KiB。");
        }

        byte[] frame = new byte[4 + bodyLength];
        byte[] lengthBytes = BitConverter.GetBytes(IPAddress.HostToNetworkOrder(bodyLength));
        Array.Copy(lengthBytes, 0, frame, 0, 4);
        frame[4] = (byte)message.Type;
        Array.Copy(content, 0, frame, 5, content.Length);
        return frame;
    }

    static bool ReceiveExactly(Socket socket, byte[] buffer)
    {
        int offset = 0;

        while (offset < buffer.Length)
        {
            int count = socket.Receive(buffer, offset, buffer.Length - offset, SocketFlags.None);

            if (count == 0)
            {
                if (offset == 0)
                {
                    return false;
                }

                throw new System.IO.IOException("连接结束，但数据尚未收完整。");
            }

            offset += count;
        }

        return true;
    }

    static NetMessage ReceiveNetMessage(Socket socket)
    {
        byte[] header = new byte[4];

        if (!ReceiveExactly(socket, header))
        {
            return null;
        }

        int bodyLength = IPAddress.NetworkToHostOrder(BitConverter.ToInt32(header, 0));

        if (bodyLength < 1 || bodyLength > MaxBodyLength)
        {
            throw new System.IO.IOException("消息长度不合法。");
        }

        byte[] body = new byte[bodyLength];

        if (!ReceiveExactly(socket, body))
        {
            throw new System.IO.IOException("消息正文尚未收完整，连接就结束了。");
        }

        MessageType type = (MessageType)body[0];

        if (type != MessageType.Chat && type != MessageType.MoveInput)
        {
            throw new System.IO.IOException("未知的消息类型。");
        }

        string content = Encoding.UTF8.GetString(body, 1, body.Length - 1);
        return new NetMessage { Type = type, Content = content };
    }
}
