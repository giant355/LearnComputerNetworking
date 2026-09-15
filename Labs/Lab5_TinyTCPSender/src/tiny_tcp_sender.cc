#include "tiny_tcp_sender.hh"

#include <algorithm>
#include <utility>

namespace tinytcp {

std::size_t TcpSenderSegment::sequence_length() const
{
    // TODO 阶段 1：payload 字节数，加上 SYN 和 FIN 各自占用的位置。
    return (payload.size() + syn + fin);
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
    //
    // -----------------------------第一版生疏错误代码---------------------------------------
    //uint64_t window_right = (acknowledged_abs_ + (advertised_window_ == 0 ? 1 : advertised_window_));
    //uint64_t available = window_right - next_seqno_abs_;

    //size_t size_to_send = std::min(mss_, available);

    //TcpSenderSegment segment;
    //if (!syn_sent_)
    //{
    //    size_to_send -= 1;
    //    segment = { Wrap32::wrap(next_seqno_abs_, isn_), true, false, "" };
    //    segments_to_send_.push_back(segment);
    //    outstanding_.push_back(OutstandingSegment{ segment, next_seqno_abs_,0});
    //    next_seqno_abs_++;
    //}
    //else
    //{
    //    if (size_to_send < stream_.bytes_buffered())
    //    {
    //        std::string payload = stream_.peek(size_to_send);
    //        segment = { Wrap32{next_seqno_abs_,isn_},false,false,payload };
    //        segments_to_send_.push_back(segment);
    //        outstanding_.push_back(OutstandingSegment{ segment, next_seqno_abs_,payload.size()});
    //        next_seqno_abs_ += payload.size();
    //    }
    //    else if (size_to_send >= stream_.bytes_buffered())
    //    {
    //        std::string payload = stream_.peek(stream_.bytes_buffered()));
    //        segment = { Wrap32{next_seqno_abs_,isn_},false,true,payload };
    //        segments_to_send_.push_back(segment);
    //        outstanding_.push_back(OutstandingSegment{ segment, next_seqno_abs_,payload.size() });
    //        next_seqno_abs_ += (payload.size() + 1);
    //    }
    //}
    //---------------------------------------------------------------------------------------
    uint64_t effective_window = advertised_window_ == 0 ? 1 : advertised_window_;
    uint64_t window_right = acknowledged_abs_ + effective_window;

    //window_right could be on the left side of nextStep
    while (window_right > next_seqno_abs_)
    {
        //available isn't the meaning that the size can payload take,the size of payload also be restricted by mss_ and stream_.bytes_buffered
        uint64_t available = window_right - next_seqno_abs_;
        TcpSenderSegment segment;
        segment.seqno = wrap(next_seqno_abs_, isn_);
        //try inserting syn
        if (!syn_sent_)
        {
            segment.syn = true;
            syn_sent_ = true;
            available--;
        }
        //try inserting payload
        size_t payload_size = std::min({ mss_, static_cast<size_t>(available), stream_.bytes_buffered() });
        if (payload_size > 0)
        {
            segment.payload = std::string(stream_.peek(payload_size));
            stream_.pop(payload_size);
            available -= payload_size;
        }
        //also try fin
        //conditions:writer closed,availble>0,bytes_buffered==0,fin_sent_==falase
        if (stream_.is_closed() && available > 0 && stream_.bytes_buffered() == 0 && !fin_sent_)
        {
            segment.fin = true;
            fin_sent_ = true;
            available--;
        }
        //to prevent infinite loops
        if (segment.sequence_length() == 0)
        {
            break;
        }

        uint64_t absolute_start = next_seqno_abs_;
        uint64_t length = segment.sequence_length();
        bool was_empty = outstanding_.empty();

        segments_to_send_.push_back(segment);

        outstanding_.push_back(OutstandingSegment{segment,absolute_start,length});

        next_seqno_abs_ += segment.sequence_length();

        //there is only one scenario that 'push' cause the timer to be reset:outstanding trans from empty to non-empty
        if (was_empty) {
            timer_running_ = true;
            timer_elapsed_ms_ = 0;
        }
    }
}

void TcpSender::receive(const TcpReceiverMessage& message)
{
    // TODO 阶段 3：没有 ACK 时不建立确认位置；先 unwrap，再拒绝超过 next_seqno_abs_ 的非法 ACK。
    // TODO 阶段 3：合法消息更新 advertised_window_；重复/旧 ACK 不让 acknowledged_abs_ 后退，也不重置 RTO。
    // TODO 阶段 3：新 ACK 清理已经被完整覆盖的 outstanding，重置 RTO 与连续重传次数。
    // TODO 阶段 3：若仍有 outstanding，计时器从零盯住新的最旧段；否则停止。最后再次 push()。
    if (message.ackno == std::nullopt)
        return;
    uint64_t absolute_ack = unwrap(Wrap32{ message.ackno.value()}, isn_, next_seqno_abs_);

    if (absolute_ack > next_seqno_abs_)
        return;

    advertised_window_ = message.window_size;
    if (absolute_ack > acknowledged_abs_)
    {
        acknowledged_abs_ = absolute_ack;

        while (!outstanding_.empty())
        {
            uint64_t segment_end = outstanding_.front().absolute_start + outstanding_.front().length;
            if (segment_end > acknowledged_abs_)
            {
                break;
            }
            outstanding_.pop_front();
        }

        current_rto_ms_ = initial_rto_ms_;
        consecutive_retransmissions_ = 0;
        timer_elapsed_ms_ = 0;

        timer_running_ = !outstanding_.empty();
    }

    push();
}

void TcpSender::tick(const std::uint64_t elapsed_ms)
{
    // TODO 阶段 4：没有 outstanding 或计时器未运行时不计时。
    // TODO 阶段 4：到达 RTO 后重传 outstanding 队首，但不重复保存，也不推进 next_seqno_abs_。
    // TODO 阶段 4：非零窗口才增加连续重传次数并让 RTO 加倍；最后把本轮计时清零。
    if (outstanding_.empty() || !timer_running_)
        return;

    timer_elapsed_ms_ += elapsed_ms;

    if (timer_elapsed_ms_ >= current_rto_ms_)
    {
        segments_to_send_.push_back(outstanding_.front().segment);
        timer_elapsed_ms_ = 0;
        if (advertised_window_ != 0)
        {
            consecutive_retransmissions_ += 1;
            current_rto_ms_ *= 2;
        }
    }
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
