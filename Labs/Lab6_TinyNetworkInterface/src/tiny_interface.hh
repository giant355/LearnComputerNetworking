#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace tinylink {

struct IPv4Address {
    std::uint32_t value {};

    /// <summary>由四个八位组构造教学用 IPv4 地址。</summary>
    [[nodiscard]] static IPv4Address from_octets(std::uint8_t a, std::uint8_t b, std::uint8_t c, std::uint8_t d);

    /// <summary>把地址显示成常见的点分十进制形式。</summary>
    [[nodiscard]] std::string to_string() const;

    bool operator==(const IPv4Address&) const = default;
};

struct IPv4AddressHash {
    [[nodiscard]] std::size_t operator()(IPv4Address address) const noexcept;
};

using MacAddress = std::string;
inline const MacAddress broadcast_mac {"FF:FF:FF:FF:FF:FF"};

struct Datagram {
    std::string id;
    IPv4Address source;
    IPv4Address destination;
    std::uint8_t ttl {64};
    std::string payload;
};

enum class EtherType : std::uint16_t {
    ipv4 = 0x0800,
    arp = 0x0806,
};

enum class ArpOpcode {
    request,
    reply,
};

struct ArpMessage {
    ArpOpcode opcode {ArpOpcode::request};
    IPv4Address sender_ip;
    MacAddress sender_mac;
    IPv4Address target_ip;
};

struct EthernetFrame {
    MacAddress destination;
    MacAddress source;
    EtherType type {EtherType::ipv4};
    std::optional<Datagram> datagram;
    std::optional<ArpMessage> arp;
    bool fcs_ok {true};
};

enum class SendResult {
    frame_ready,
    waiting_for_arp,
    no_route,
    queue_full,
};

class NetworkInterface {
public:
    /// <summary>创建一张带本地前缀、可选默认网关和有限待解析队列的教学网络接口。</summary>
    /// <param name="ip">接口自己的 IPv4 地址。</param>
    /// <param name="prefix_length">直连网络的前缀长度，范围为 0～32。</param>
    /// <param name="mac">接口自己的 Ethernet MAC 地址。</param>
    /// <param name="default_gateway">跨网段时使用的默认网关；没有时跨网段发送返回 no_route。</param>
    /// <param name="max_waiting_per_neighbor">每个未知下一跳最多暂存的数据报数。</param>
    NetworkInterface(IPv4Address ip, std::uint8_t prefix_length, MacAddress mac, std::optional<IPv4Address> default_gateway, std::size_t max_waiting_per_neighbor = 4);

    /// <summary>根据最终目标选择下一跳，并立即制帧或进入对应邻居的 ARP 等待状态。</summary>
    /// <param name="datagram">端到端 IPv4 数据报；本函数不会改写其最终目标。</param>
    /// <returns>已经制帧、等待 ARP、没有路由或等待队列已满。</returns>
    SendResult send_datagram(Datagram datagram);

    /// <summary>处理当前链路收到的 Ethernet 帧，过滤坏帧与无关帧，并分流 IPv4/ARP。</summary>
    /// <param name="frame">当前链路收到的教学 Ethernet 帧。</param>
    /// <returns>需要交给本机 IP 层的 IPv4 数据报；其他情况返回 nullopt。</returns>
    [[nodiscard]] std::optional<Datagram> recv_frame(const EthernetFrame& frame);

    /// <summary>推进内部时钟，处理 ARP 缓存过期、未解析邻居重试和最终丢弃。</summary>
    /// <param name="elapsed_ms">从上次调用后经过的毫秒数。</param>
    void tick(std::uint64_t elapsed_ms);

    /// <summary>取得并清空接口已经制作好的全部 Ethernet 帧。</summary>
    [[nodiscard]] std::vector<EthernetFrame> take_frames_to_send();

    /// <summary>只读查询：目标 IP 当前应直接交付还是交给默认网关。</summary>
    [[nodiscard]] std::optional<IPv4Address> next_hop_for(IPv4Address destination) const;

    /// <summary>只读查询：某个邻居当前是否具有仍有效的 IP 到 MAC 映射。</summary>
    [[nodiscard]] std::optional<MacAddress> cached_mac(IPv4Address neighbor) const;

    /// <summary>只读查询：某个下一跳当前等待 ARP 的数据报数量。</summary>
    [[nodiscard]] std::size_t waiting_count(IPv4Address neighbor) const;

    /// <summary>只读查询：因为没有路由、队列满或 ARP 最终失败而丢弃的数据报总数。</summary>
    [[nodiscard]] std::size_t dropped_count() const;

private:
    struct NeighborState {
        std::optional<MacAddress> mac;
        std::uint64_t cache_expires_at_ms {};
        std::deque<Datagram> waiting;
        std::optional<std::uint64_t> last_request_ms;
        std::size_t request_attempts {};
    };

    static constexpr std::uint64_t cache_lifetime_ms {30'000};
    static constexpr std::uint64_t arp_retry_ms {5'000};
    static constexpr std::size_t max_arp_attempts {3};

    [[nodiscard]] bool same_network(IPv4Address other) const;
    [[nodiscard]] bool has_live_mapping(const NeighborState& state) const;
    void emit_ipv4_frame(Datagram datagram, const MacAddress& destination_mac);
    void emit_arp_request(IPv4Address target_ip);
    void emit_arp_reply(const ArpMessage& request);
    void learn_mapping(IPv4Address ip, const MacAddress& mac);
    void flush_waiting(IPv4Address neighbor);

    IPv4Address ip_;
    std::uint8_t prefix_length_ {};
    MacAddress mac_;
    std::optional<IPv4Address> default_gateway_;
    std::size_t max_waiting_per_neighbor_ {};
    std::uint64_t now_ms_ {};
    std::size_t dropped_count_ {};

    std::unordered_map<IPv4Address, NeighborState, IPv4AddressHash> neighbors_;
    std::deque<EthernetFrame> frames_to_send_;
};

} // namespace tinylink
