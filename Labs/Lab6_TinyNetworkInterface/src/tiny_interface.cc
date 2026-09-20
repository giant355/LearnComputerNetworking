#include "tiny_interface.hh"

#include <limits>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace tinylink {

IPv4Address IPv4Address::from_octets(const std::uint8_t a, const std::uint8_t b, const std::uint8_t c, const std::uint8_t d)
{
    return IPv4Address {(static_cast<std::uint32_t>(a) << 24U) | (static_cast<std::uint32_t>(b) << 16U) | (static_cast<std::uint32_t>(c) << 8U) | static_cast<std::uint32_t>(d)};
}

std::string IPv4Address::to_string() const
{
    std::ostringstream output;
    output << ((value >> 24U) & 0xffU) << '.' << ((value >> 16U) & 0xffU) << '.' << ((value >> 8U) & 0xffU) << '.' << (value & 0xffU);
    return output.str();
}

std::size_t IPv4AddressHash::operator()(const IPv4Address address) const noexcept
{
    return static_cast<std::size_t>(address.value);
}

NetworkInterface::NetworkInterface(IPv4Address ip, const std::uint8_t prefix_length, MacAddress mac, std::optional<IPv4Address> default_gateway, const std::size_t max_waiting_per_neighbor)
    : ip_(ip)
    , prefix_length_(prefix_length)
    , mac_(std::move(mac))
    , default_gateway_(default_gateway)
    , max_waiting_per_neighbor_(max_waiting_per_neighbor)
{
    if (prefix_length_ > 32) {
        throw std::invalid_argument("prefix length must be at most 32");
    }
    if (max_waiting_per_neighbor_ == 0) {
        throw std::invalid_argument("waiting capacity must be positive");
    }
}

bool NetworkInterface::same_network(const IPv4Address other) const
{
    // TODO 1：比较 ip_ 与 other 的前 prefix_length_ 位。
    // /0 匹配所有地址；/32 只有完整地址相同才算直连。
    static_cast<void>(other);
    return false;
}

std::optional<IPv4Address> NetworkInterface::next_hop_for(const IPv4Address destination) const
{
    // TODO 2：直连目标返回 destination；跨网段返回 default_gateway_；没有网关返回 nullopt。
    static_cast<void>(destination);
    return std::nullopt;
}

bool NetworkInterface::has_live_mapping(const NeighborState& state) const
{
    return state.mac.has_value() && state.cache_expires_at_ms > now_ms_;
}

void NetworkInterface::emit_ipv4_frame(Datagram datagram, const MacAddress& destination_mac)
{
    frames_to_send_.push_back(EthernetFrame {.destination = destination_mac, .source = mac_, .type = EtherType::ipv4, .datagram = std::move(datagram), .arp = std::nullopt, .fcs_ok = true});
}

void NetworkInterface::emit_arp_request(const IPv4Address target_ip)
{
    frames_to_send_.push_back(EthernetFrame {.destination = broadcast_mac, .source = mac_, .type = EtherType::arp, .datagram = std::nullopt, .arp = ArpMessage {.opcode = ArpOpcode::request, .sender_ip = ip_, .sender_mac = mac_, .target_ip = target_ip}, .fcs_ok = true});
}

void NetworkInterface::emit_arp_reply(const ArpMessage& request)
{
    frames_to_send_.push_back(EthernetFrame {.destination = request.sender_mac, .source = mac_, .type = EtherType::arp, .datagram = std::nullopt, .arp = ArpMessage {.opcode = ArpOpcode::reply, .sender_ip = ip_, .sender_mac = mac_, .target_ip = request.sender_ip}, .fcs_ok = true});
}

void NetworkInterface::learn_mapping(const IPv4Address ip, const MacAddress& mac)
{
    auto& state = neighbors_[ip];
    state.mac = mac;
    state.cache_expires_at_ms = now_ms_ + cache_lifetime_ms;
    state.last_request_ms.reset();
    state.request_attempts = 0;
}

void NetworkInterface::flush_waiting(const IPv4Address neighbor)
{
    const auto found = neighbors_.find(neighbor);
    if (found == neighbors_.end() || !has_live_mapping(found->second)) {
        return;
    }

    auto& state = found->second;
    while (!state.waiting.empty()) {
        Datagram datagram = std::move(state.waiting.front());
        state.waiting.pop_front();
        emit_ipv4_frame(std::move(datagram), *state.mac);
    }
}

SendResult NetworkInterface::send_datagram(Datagram datagram)
{
    // TODO 3：发送入口路线图（不要跳步）：
    // 数据报到来
    //   → next_hop_for(final destination)
    //      ├─ 无下一跳：dropped_count_++，返回 no_route
    //      └─ 得到 next_hop
    //           → 该邻居缓存是否有效？
    //              ├─ 是：制作 IPv4 Ethernet 帧，返回 frame_ready
    //              └─ 否：等待队列是否已满？
    //                   ├─ 是：dropped_count_++，返回 queue_full
    //                   └─ 否：数据报入队；若尚未请求则广播 ARP，记录时间与次数；返回 waiting_for_arp
    static_cast<void>(datagram);
    return SendResult::no_route;
}

std::optional<Datagram> NetworkInterface::recv_frame(const EthernetFrame& frame)
{
    // TODO 4：收帧入口路线图：
    // 帧到来
    //   → FCS 是否正确、目标 MAC 是否为本机或广播？否则忽略
    //   → EtherType
    //      ├─ IPv4：只接受目标 MAC 为本机且 datagram 存在，向上返回
    //      └─ ARP：arp 是否存在？
    //           → 学习 sender IP→MAC，并冲刷这个 sender 对应的等待队列
    //           → 若是询问 ip_ 的 request，再制作单播 reply
    // ARP 分支不向 IP 层返回数据报。
    static_cast<void>(frame);
    return std::nullopt;
}

void NetworkInterface::tick(const std::uint64_t elapsed_ms)
{
    // TODO 5：时间事件路线图：
    // now_ms_ 前进
    //   → 每个邻居：缓存到期则清除 mac
    //   → 若没有等待数据：继续
    //   → 距上次请求不足 5 秒：继续
    //   → 请求次数少于 3：再发 ARP，并更新时间/次数
    //   → 已发满 3 次：丢弃该邻居全部等待数据报，累计 dropped_count_，重置请求状态
    static_cast<void>(elapsed_ms);
}

std::vector<EthernetFrame> NetworkInterface::take_frames_to_send()
{
    std::vector<EthernetFrame> result;
    result.reserve(frames_to_send_.size());
    while (!frames_to_send_.empty()) {
        result.push_back(std::move(frames_to_send_.front()));
        frames_to_send_.pop_front();
    }
    return result;
}

std::optional<MacAddress> NetworkInterface::cached_mac(const IPv4Address neighbor) const
{
    const auto found = neighbors_.find(neighbor);
    if (found == neighbors_.end() || !has_live_mapping(found->second)) {
        return std::nullopt;
    }
    return found->second.mac;
}

std::size_t NetworkInterface::waiting_count(const IPv4Address neighbor) const
{
    const auto found = neighbors_.find(neighbor);
    return found == neighbors_.end() ? 0 : found->second.waiting.size();
}

std::size_t NetworkInterface::dropped_count() const
{
    return dropped_count_;
}

} // namespace tinylink
