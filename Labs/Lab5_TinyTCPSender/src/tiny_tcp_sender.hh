#pragma once

#include "tiny_stream.hh"
#include "wrapping_integers.hh"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <vector>

namespace tinytcp {

struct TcpSenderSegment {
    Wrap32 seqno { 0 };
    bool syn { false };
    bool fin { false };
    std::string payload;

    /// <summary>返回本段占用的 TCP 序号空间长度；SYN、FIN 各占一个位置。</summary>
    [[nodiscard]] std::size_t sequence_length() const;
};

struct TcpReceiverMessage {
    std::optional<Wrap32> ackno;
    std::uint16_t window_size { 0 };
};

class TcpSender {
public:
    /// <summary>创建简化 TCP 发送端；连接开始前按一个序号位置的初始窗口发送 SYN。</summary>
    /// <param name="capacity">应用发送 ByteStream 的容量。</param>
    /// <param name="isn">本方向写入 TCP 首部时使用的初始序号。</param>
    /// <param name="initial_rto_ms">第一次等待 ACK 的重传超时时间，单位毫秒。</param>
    /// <param name="mss">单个段最多携带的 payload 字节数。</param>
    TcpSender(std::size_t capacity, Wrap32 isn, std::uint64_t initial_rto_ms, std::size_t mss = 3);

    /// <summary>取得应用写入数据的发送 ByteStream。</summary>
    [[nodiscard]] tinystream::ByteStream& stream();

    /// <summary>按当前窗口尽量制作新段；新段会保存到 outstanding 并进入待发送队列。</summary>
    void push();

    /// <summary>处理接收端的累计 ACK 与窗口通告；非法 ACK 不改变发送状态。</summary>
    /// <param name="message">接收端返回的 ACK 和当前接收窗口。</param>
    void receive(const TcpReceiverMessage& message);

    /// <summary>推进重传计时器；超时时重传最旧未确认段。</summary>
    /// <param name="elapsed_ms">自上次 tick 以来经过的毫秒数。</param>
    void tick(std::uint64_t elapsed_ms);

    /// <summary>一次性取走等待交给 IP 的段；不删除 outstanding 中的重传副本。</summary>
    [[nodiscard]] std::vector<TcpSenderSegment> take_segments_to_send();

    /// <summary>返回已经发送但尚未被累计 ACK 覆盖的序号位置数。</summary>
    [[nodiscard]] std::uint64_t sequence_numbers_in_flight() const;

    /// <summary>返回自最近一次推进 ACK 之后发生的连续重传次数。</summary>
    [[nodiscard]] std::uint64_t consecutive_retransmissions() const;

    /// <summary>返回当前 RTO，供观察指数退避。</summary>
    [[nodiscard]] std::uint64_t current_rto_ms() const;

    /// <summary>返回下一份新内容的 64 位内部绝对序号。</summary>
    [[nodiscard]] std::uint64_t next_seqno_abs() const;

    /// <summary>返回当前保存的未确认段数量。</summary>
    [[nodiscard]] std::size_t outstanding_count() const;

private:
    struct OutstandingSegment {
        TcpSenderSegment segment;
        std::uint64_t absolute_start { 0 };
        std::uint64_t length { 0 };
    };

    tinystream::ByteStream stream_;
    Wrap32 isn_;
    std::uint64_t initial_rto_ms_;
    std::uint64_t current_rto_ms_;
    std::size_t mss_;
    std::uint64_t next_seqno_abs_ { 0 };
    std::uint64_t acknowledged_abs_ { 0 };
    std::uint64_t advertised_window_ { 1 };
    std::uint64_t timer_elapsed_ms_ { 0 };
    std::uint64_t consecutive_retransmissions_ { 0 };
    bool syn_sent_ { false };
    bool fin_sent_ { false };
    bool timer_running_ { false };
    std::deque<OutstandingSegment> outstanding_;
    std::deque<TcpSenderSegment> segments_to_send_;
};

} // namespace tinytcp
