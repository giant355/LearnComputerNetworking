#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace tinyarp {

// 为了把注意力放在状态机上，教学模型直接把地址保存为可读字符串。
using IpAddress = std::string;
using MacAddress = std::string;

inline const MacAddress broadcast_mac {"FF:FF:FF:FF:FF:FF"};

// 一份极简 IPv4 数据报：id 只方便测试观察；final_destination 绝不被 ARP 改写。
struct Datagram {
    std::string id;
    IpAddress final_destination;
};

enum class FrameType {
    ipv4,
    arp_request,
    arp_reply,
};

// ARP 中本课真正需要观察的四项身份信息。
struct ArpMessage {
    IpAddress sender_ip;
    MacAddress sender_mac;
    IpAddress target_ip;
};

// 这是内存中的教学帧，不做真实 Ethernet 字节序列化。
// ipv4 帧只使用 datagram；ARP 帧只使用 arp。
struct EthernetFrame {
    MacAddress destination;
    MacAddress source;
    FrameType type;
    std::optional<Datagram> datagram;
    std::optional<ArpMessage> arp;
    bool fcs_ok {true};
};

class NetworkInterface {
public:
    /// <summary>创建一张拥有指定 IPv4 地址与 MAC 地址的教学网络接口。</summary>
    /// <param name="ip">接口自身的 IPv4 地址，不是默认网关地址。</param>
    /// <param name="mac">接口自身的 Ethernet MAC 地址。</param>
    NetworkInterface(IpAddress ip, MacAddress mac);

    /// <summary>响应 IP 层发送事件：把数据报直接封装，或等待下一跳的 ARP 映射。</summary>
    /// <param name="datagram">端到端 IPv4 数据报；本函数不改写其最终目标。</param>
    /// <param name="next_hop">路由表选出的当前下一跳 IP，用作缓存与等待队列的键。</param>
    void send_datagram(Datagram datagram, const IpAddress& next_hop);

    /// <summary>响应当前 Ethernet 链路的收帧事件，并按帧类型交给 IPv4 或 ARP 处理。</summary>
    /// <param name="frame">收到的教学 Ethernet 帧；非本机/非广播或 FCS 失败的帧会被忽略。</param>
    /// <returns>属于本接口的有效 IPv4 数据报；ARP、无关帧或坏帧返回 nullopt。</returns>
    [[nodiscard]] std::optional<Datagram> recv_frame(const EthernetFrame& frame);

    /// <summary>响应时间流逝事件，推进内部时钟并删除已过期的 ARP 缓存项。</summary>
    /// <param name="elapsed_ms">自上次调用后经过的毫秒数；时间经过本身不主动发送 ARP。</param>
    void tick(std::uint64_t elapsed_ms);

    /// <summary>取走已经制作完成的 Ethernet 帧，模拟交给链路发送。</summary>
    /// <returns>按制作顺序返回帧，并清空内部待发送帧队列；只供测试观察。</returns>
    [[nodiscard]] std::vector<EthernetFrame> take_frames_to_send();

    /// <summary>查询当前有效的 ARP 缓存映射；只供测试观察。</summary>
    /// <param name="ip">要查询的当前链路邻居 IP。</param>
    /// <returns>仍有效的 MAC；无记录或已过期时返回 nullopt。</returns>
    [[nodiscard]] std::optional<MacAddress> cached_mac(const IpAddress& ip) const;

    /// <summary>查询某个下一跳正在等待地址解析的数据报数量；只供测试观察。</summary>
    /// <param name="next_hop">待解析队列的键，即当前下一跳 IP。</param>
    /// <returns>该队列中的数据报数量。</returns>
    [[nodiscard]] std::size_t waiting_count(const IpAddress& next_hop) const;

private:
    struct CacheEntry {
        MacAddress mac;
        std::uint64_t expires_at_ms {};
    };

    static constexpr std::uint64_t cache_lifetime_ms {30'000};
    static constexpr std::uint64_t request_suppression_ms {5'000};

    [[nodiscard]] bool cache_has_live_entry(const IpAddress& ip) const;
    void send_ipv4_frame(Datagram datagram, const MacAddress& destination_mac);
    void send_arp_request(const IpAddress& target_ip);
    void learn_arp_mapping(const IpAddress& ip, const MacAddress& mac);
    void flush_waiting_datagrams(const IpAddress& next_hop);

    IpAddress ip_;
    MacAddress mac_;
    std::uint64_t now_ms_ {};

    std::unordered_map<IpAddress, CacheEntry> arp_cache_;
    std::unordered_map<IpAddress, std::deque<Datagram>> waiting_;
    std::unordered_map<IpAddress, std::uint64_t> last_arp_request_ms_;
    std::deque<EthernetFrame> frames_to_send_;
};

} // namespace tinyarp
