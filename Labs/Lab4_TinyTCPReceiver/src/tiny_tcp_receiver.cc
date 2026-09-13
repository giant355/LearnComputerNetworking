#include "tiny_tcp_receiver.hh"

namespace tinytcp {

TcpReceiver::TcpReceiver(const std::size_t capacity)
    : capacity_(capacity)
    , reassembler_(capacity)
{}

void TcpReceiver::receive(const TcpSegment& segment)
{
    // TODO 阶段 3：SYN 到达前忽略普通段；首个合法 SYN 记录本方向 ISN。
    // TODO 阶段 3：unwrap SEQ，把 payload 起点换算成 first_index，并把 FIN 作为最后标记交给 Reassembler。
    (void)segment;
}

std::optional<Wrap32> TcpReceiver::ackno() const
{
    // TODO 阶段 4：ACK 覆盖 SYN、连续 payload，以及已经连续到达的 FIN。
    return std::nullopt;
}

std::size_t TcpReceiver::window_size() const
{
    // TODO 阶段 4：本 Lab 规定 ByteStream 未读字节与待重组字节共同占用 capacity_。
    return capacity_;
}

tinyreassembler::Reassembler& TcpReceiver::reassembler()
{
    return reassembler_;
}

const tinyreassembler::Reassembler& TcpReceiver::reassembler() const
{
    return reassembler_;
}

} // namespace tinytcp
