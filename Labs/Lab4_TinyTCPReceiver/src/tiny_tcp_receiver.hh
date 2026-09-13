#pragma once

#include "tiny_reassembler.hh"
#include "wrapping_integers.hh"

#include <cstddef>
#include <optional>
#include <string>

namespace tinytcp {

struct TcpSegment {
    Wrap32 seqno { 0 };
    bool syn { false };
    bool fin { false };
    std::string payload;
};

class TcpReceiver {
public:
    /// <summary>创建一个共享 capacity 容量的简化 TCP 接收端。</summary>
    /// <param name="capacity">ByteStream 未读字节与 Reassembler 待重组字节的合计上限。</param>
    explicit TcpReceiver(std::size_t capacity);

    /// <summary>接收一份 TCP 段；SYN 建立 ISN，payload 按序号换算后交给 Reassembler。</summary>
    /// <param name="segment">包含线上序号、SYN/FIN 标志和 payload 的简化 TCP 段。</param>
    void receive(const TcpSegment& segment);

    /// <summary>返回当前应通告的 32 位累计 ACK；合法 SYN 到达前没有 ACK。</summary>
    /// <returns>下一个期待的线上序号；尚未建立 ISN 时返回空。</returns>
    [[nodiscard]] std::optional<Wrap32> ackno() const;

    /// <summary>返回当前剩余接收容量，不把它误当成 ACK 或累计传输量。</summary>
    [[nodiscard]] std::size_t window_size() const;

    /// <summary>取得内部 Reassembler，供应用 Reader 观察或消费已经连续重组的字节。</summary>
    [[nodiscard]] tinyreassembler::Reassembler& reassembler();

    /// <summary>以只读方式取得内部 Reassembler。</summary>
    [[nodiscard]] const tinyreassembler::Reassembler& reassembler() const;

private:
    std::size_t capacity_;
    tinyreassembler::Reassembler reassembler_;
    std::optional<Wrap32> isn_;
};

} // namespace tinytcp
