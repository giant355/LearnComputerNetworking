using System;
using System.Net;
using System.Net.Sockets;
using System.Text;
using System.Threading;

class Program
{
    static readonly Socket serverSocket = new Socket(AddressFamily.InterNetwork, SocketType.Stream, ProtocolType.Tcp);
    static readonly List<ClientSocket> clientSockets = new List<ClientSocket>();
    static int nextClientId = 1;
    static readonly List<Thread> receiveThreads = new List<Thread>();
    public sealed class ClientSocket
    {
        private readonly Socket socket;
        public int Id { get; }

        public ClientSocket(Socket socket, int id)
        {
            this.socket = socket;
            Id = id;
        }

        /// <summary>关闭当前客户端连接，并释放它占用的 Socket 资源。</summary>
        public void Close()
        {
            socket.Close();
        }

        /// <summary>把数组中指定范围的字节全部提交给 Socket；失败时抛出异常。</summary>
        /// <param name="data">要发送的字节数组。</param>
        /// <param name="offset">从哪个数组下标开始发送。</param>
        /// <param name="count">本次要发送的字节数。</param>
        public void Send(byte[] data, int offset, int count)
        {
            int end = offset + count;
            while (offset < end)
            {
                int sent = socket.Send(data, offset, end - offset, SocketFlags.None);   
                if (sent == 0) throw new System.IO.IOException("发送未能继续，连接无法完成本次发送。");
                offset += sent;
            }
        }

        /// <summary>持续接收并回显当前连接的数据；连接结束后关闭 Socket。</summary>
        public void ReceiveLoop()
        {
            try
            {
                while (true)
                {
                    NetMessage? message = ReceiveNetMessage(socket);

                    if (message == null)
                    {
                        Console.WriteLine($"客户端 {Id} 正常结束发送");
                        break;
                    }

                    if (message.Type == MessageType.Chat)
                    {
                        Console.WriteLine($"客户端 {Id} 发来聊天：{message.Content}");

                        NetMessage reply = new NetMessage
                        {
                            Type = MessageType.Chat,
                            Content = message.Content + ",服务端已收到"
                        };

                        byte[] data = EncodeMessage(reply);
                        Send(data, 0, data.Length);
                    }
                    else if (message.Type == MessageType.MoveInput)
                    {
                        Console.WriteLine($"客户端 {Id} 发来移动输入：{message.Content}");
                    }
                }
            }
            catch (ObjectDisposedException)
            {
                Console.WriteLine($"客户端 {Id}：连接被本机关闭");
            }
            catch (SocketException ex)
            {
                Console.WriteLine($"客户端 {Id}：收发结束，{ex.SocketErrorCode}");
            }
            catch (System.IO.IOException ex)
            {
                Console.WriteLine($"客户端 {Id}：{ex.Message}");
            }
            finally
            {
                Close();
            }
        }
    }
    static void Main()
    {
        try
        {
            IPEndPoint address = new IPEndPoint(IPAddress.Loopback, 8080);
            serverSocket.Bind(address);
            serverSocket.Listen(10);

            Console.WriteLine("服务器正在监听 127.0.0.1:8080");

            Thread acceptThread = new Thread(AcceptClients);
            acceptThread.Start();

            Console.WriteLine("按回车停止监听");
            Console.ReadLine();

            serverSocket.Close();
            acceptThread.Join();

            foreach (ClientSocket connection in clientSockets)
            {
                connection.Close();
            }

            foreach (Thread thread in receiveThreads)
            {
                thread.Join();
            }

            Console.WriteLine("服务器的所有线程已结束");
        }
        catch (SocketException ex)
        {
            Console.WriteLine($"启动失败：{ex.SocketErrorCode}，{ex.Message}");
        }
        finally
        {
            serverSocket.Close();
        }
    }

    static void AcceptClients()
    {
        try
        {
            while (true)
            {
                Socket client = serverSocket.Accept();
                ClientSocket connection = new ClientSocket(client, nextClientId++);
                Console.WriteLine($"客户端 {connection.Id} 已连接：{client.RemoteEndPoint}");

                clientSockets.Add(connection);

                Thread receiveThread = new Thread(connection.ReceiveLoop);
                receiveThread.IsBackground = true;
                receiveThreads.Add(receiveThread);
                receiveThread.Start();
            }
        }
        catch (ObjectDisposedException)
        {
            Console.WriteLine("监听 Socket 已关闭，接入线程结束");
        }
        catch (SocketException ex)
        {
            Console.WriteLine($"接入操作结束：{ex.SocketErrorCode}");
        }
    }
    /// <summary>
    /// 给消息正文加上长度前缀
    /// </summary>
    /// <param name="text"></param>
    /// <returns></returns>
    static byte[] EncodeMessage(string text)
    {
        byte[] body = Encoding.UTF8.GetBytes(text);
        byte[] frame = new byte[4 + body.Length];

        byte[] lengthBytes = BitConverter.GetBytes(IPAddress.HostToNetworkOrder(body.Length));
        Array.Copy(lengthBytes, 0, frame, 0, 4);
        Array.Copy(body, 0, frame, 4, body.Length);

        return frame;
    }
    static byte[] EncodeMessage(NetMessage message)
    {
        byte[] content = Encoding.UTF8.GetBytes(message.Content);
        int bodyLength = 1 + content.Length;

        byte[] frame = new byte[4 + bodyLength];
        byte[] lengthBytes = BitConverter.GetBytes(IPAddress.HostToNetworkOrder(bodyLength));

        Array.Copy(lengthBytes, 0, frame, 0, 4);
        frame[4] = (byte)message.Type;
        Array.Copy(content, 0, frame, 5, content.Length);

        return frame;
    }

    /// <summary>
    /// 用于精确接收长度前缀，或者正文
    /// </summary>
    /// <param name="socket"></param>
    /// <param name="buffer"></param>
    /// <returns></returns>
    /// <exception cref="System.IO.IOException"></exception>
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

    static string? ReceiveMessage(Socket socket)
    {
        byte[] header = new byte[4];

        if (!ReceiveExactly(socket, header))
        {
            return null;
        }

        int bodyLength = IPAddress.NetworkToHostOrder(BitConverter.ToInt32(header, 0));

        if (bodyLength < 0 || bodyLength > 64 * 1024)
        {
            throw new System.IO.IOException("消息长度不合法。");
        }

        byte[] body = new byte[bodyLength];

        if (!ReceiveExactly(socket, body))
        {
            throw new System.IO.IOException("收到长度前缀后，正文尚未收完整，连接就结束了。");
        }

        return Encoding.UTF8.GetString(body);
    }

    static NetMessage? ReceiveNetMessage(Socket socket)
    {
        byte[] header = new byte[4];

        if (!ReceiveExactly(socket, header))
        {
            return null;
        }

        int bodyLength = IPAddress.NetworkToHostOrder(BitConverter.ToInt32(header, 0));

        if (bodyLength < 1 || bodyLength > 64 * 1024)
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
