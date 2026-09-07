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
    NetworkInterface(IpAddress ip, MacAddress mac);

    // 事件 1：IP 层已经选择 next_hop，要求接口把数据报发往当前链路。
    void send_datagram(Datagram datagram, const IpAddress& next_hop);

    // 事件 2：当前 Ethernet 链路送来一帧。
    // 若这是一份属于本接口的有效 IPv4 帧，返回其 IP 数据报；其他情况返回 nullopt。
    [[nodiscard]] std::optional<Datagram> recv_frame(const EthernetFrame& frame);

    // 事件 3：让内部时间向前走。教学规则：ARP 缓存 30 秒后过期。
    void tick(std::uint64_t elapsed_ms);

    // 把已经制作好的帧全部取走，模拟“交给链路发送”。只供测试观察。
    [[nodiscard]] std::vector<EthernetFrame> take_frames_to_send();

    // 下列函数只供测试观察状态，不是协议中的远程查询。
    [[nodiscard]] std::optional<MacAddress> cached_mac(const IpAddress& ip) const;
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
