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