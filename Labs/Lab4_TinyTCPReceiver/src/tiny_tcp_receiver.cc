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
    if (isn_ == std::nullopt)
    {
        if (segment.syn == false)
            return;
        if (segment.syn == true)
            isn_ = segment.seqno;
    }
    uint64_t checkpoint = reassembler_.first_unassembled() + 1;
    uint64_t segment_absolute = unwrap(segment.seqno, *isn_, checkpoint);

    uint64_t payload_absolute = segment_absolute + (segment.syn ? 1 : 0);
    uint64_t first_index = payload_absolute - 1;

    reassembler_.insert(first_index,segment.payload,segment.fin);
}

std::optional<Wrap32> TcpReceiver::ackno() const
{
    // TODO 阶段 4：ACK 覆盖 SYN、连续 payload，以及已经连续到达的 FIN。
    if (!isn_.has_value()) return std::nullopt;

    uint64_t absolute_ack = 1 + reassembler_.output().bytes_pushed() + (reassembler_.output().is_closed() ? 1 : 0);
    return wrap(absolute_ack, *isn_);
}

std::size_t TcpReceiver::window_size() const
{
    // TODO 阶段 4：本 Lab 规定 ByteStream 未读字节与待重组字节共同占用 capacity_。
    return (capacity_ - reassembler_.bytes_pending() - reassembler_.output().bytes_buffered());
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
