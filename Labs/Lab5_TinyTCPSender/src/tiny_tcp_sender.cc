#include "tiny_tcp_sender.hh"

#include <algorithm>
#include <utility>

namespace tinytcp {

std::size_t TcpSenderSegment::sequence_length() const
{
    // TODO 阶段 1：payload 字节数，加上 SYN 和 FIN 各自占用的位置。
    return 0;
}

TcpSender::TcpSender(const std::size_t capacity, const Wrap32 isn, const std::uint64_t initial_rto_ms, const std::size_t mss)
    : stream_(capacity)
    , isn_(isn)
    , initial_rto_ms_(initial_rto_ms)
    , current_rto_ms_(initial_rto_ms)
    , mss_(mss)
{}

tinystream::ByteStream& TcpSender::stream()
{
    return stream_;
}

void TcpSender::push()
{
    // TODO 阶段 2：把通告窗口 0 暂时视作 1，以允许零窗口探测。
    // TODO 阶段 2：在可用窗口内依次放入 SYN、payload、FIN；payload 每段不超过 mss_。
    // TODO 阶段 2：为新段设置 wrap 后的 SEQ，保存 outstanding 副本，再加入 segments_to_send_。
    // TODO 阶段 2：只有从“没有未确认段”变成“存在未确认段”时才启动计时器。
}

void TcpSender::receive(const TcpReceiverMessage& message)
{
    // TODO 阶段 3：没有 ACK 时不建立确认位置；先 unwrap，再拒绝超过 next_seqno_abs_ 的非法 ACK。
    // TODO 阶段 3：合法消息更新 advertised_window_；重复/旧 ACK 不让 acknowledged_abs_ 后退，也不重置 RTO。
    // TODO 阶段 3：新 ACK 清理已经被完整覆盖的 outstanding，重置 RTO 与连续重传次数。
    // TODO 阶段 3：若仍有 outstanding，计时器从零盯住新的最旧段；否则停止。最后再次 push()。
    (void)message;
}

void TcpSender::tick(const std::uint64_t elapsed_ms)
{
    // TODO 阶段 4：没有 outstanding 或计时器未运行时不计时。
    // TODO 阶段 4：到达 RTO 后重传 outstanding 队首，但不重复保存，也不推进 next_seqno_abs_。
    // TODO 阶段 4：非零窗口才增加连续重传次数并让 RTO 加倍；最后把本轮计时清零。
    (void)elapsed_ms;
}

std::vector<TcpSenderSegment> TcpSender::take_segments_to_send()
{
    std::vector<TcpSenderSegment> result;
    result.reserve(segments_to_send_.size());
    while (!segments_to_send_.empty()) {
        result.push_back(std::move(segments_to_send_.front()));
        segments_to_send_.pop_front();
    }
    return result;
}

std::uint64_t TcpSender::sequence_numbers_in_flight() const
{
    return next_seqno_abs_ - acknowledged_abs_;
}

std::uint64_t TcpSender::consecutive_retransmissions() const
{
    return consecutive_retransmissions_;
}

std::uint64_t TcpSender::current_rto_ms() const
{
    return current_rto_ms_;
}

std::uint64_t TcpSender::next_seqno_abs() const
{
    return next_seqno_abs_;
}

std::size_t TcpSender::outstanding_count() const
{
    return outstanding_.size();
}

} // namespace tinytcp
