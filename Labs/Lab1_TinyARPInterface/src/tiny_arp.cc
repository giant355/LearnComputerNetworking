#include "tiny_arp.hh"

#include <utility>

namespace tinyarp {

NetworkInterface::NetworkInterface(IpAddress ip, MacAddress mac)
    : ip_(std::move(ip))
    , mac_(std::move(mac))
{}

bool NetworkInterface::cache_has_live_entry(const IpAddress& ip) const
{
    const auto found = arp_cache_.find(ip);
    return found != arp_cache_.end() && found->second.expires_at_ms > now_ms_;
}

void NetworkInterface::send_ipv4_frame(
    Datagram datagram,
    const MacAddress& destination_mac
)
{
    frames_to_send_.push_back(EthernetFrame {
        .destination = destination_mac,
        .source = mac_,
        .type = FrameType::ipv4,
        .datagram = std::move(datagram),
        .arp = std::nullopt,
        .fcs_ok = true,
    });
}

void NetworkInterface::send_arp_request(const IpAddress& target_ip)
{
    frames_to_send_.push_back(EthernetFrame {
        .destination = broadcast_mac,
        .source = mac_,
        .type = FrameType::arp_request,
        .datagram = std::nullopt,
        .arp = ArpMessage {
            .sender_ip = ip_,
            .sender_mac = mac_,
            .target_ip = target_ip,
        },
        .fcs_ok = true,
    });
}

void NetworkInterface::learn_arp_mapping(const IpAddress& ip, const MacAddress& mac)
{
    arp_cache_[ip] = CacheEntry {
        .mac = mac,
        .expires_at_ms = now_ms_ + cache_lifetime_ms,
    };
}

void NetworkInterface::flush_waiting_datagrams(const IpAddress& next_hop)
{
    const auto waiting = waiting_.find(next_hop);
    const auto cache = arp_cache_.find(next_hop);

    if (waiting == waiting_.end() || cache == arp_cache_.end()) {
        return;
    }

    while (!waiting->second.empty()) {
        Datagram datagram = std::move(waiting->second.front());
        waiting->second.pop_front();
        send_ipv4_frame(std::move(datagram), cache->second.mac);
    }

    waiting_.erase(waiting);
}

void NetworkInterface::send_datagram(Datagram datagram, const IpAddress& next_hop)
{
    // TODO 阶段 1：缓存命中时直接制作 IPv4 帧。
    // TODO 阶段 1：缓存未命中时按 next_hop 排队；必要时发一份 ARP 请求。
    // 同一个 next_hop 在 5 秒内只能发一份请求。
    (void)datagram;
    (void)next_hop;
}

std::optional<Datagram> NetworkInterface::recv_frame(const EthernetFrame& frame)
{
    // TODO 阶段 3：先过滤非本机/非广播帧和 FCS 失败帧。
    // TODO 阶段 3：IPv4 帧返回 datagram；ARP 帧学习发送者映射、冲刷等待队列。
    // TODO 阶段 4：若 ARP 请求目标是 ip_，制作单播 ARP 回复。
    (void)frame;
    return std::nullopt;
}

void NetworkInterface::tick(const std::uint64_t elapsed_ms)
{
    // TODO 阶段 5：推进 now_ms_，删除已经到期的缓存项。
    (void)elapsed_ms;
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

std::optional<MacAddress> NetworkInterface::cached_mac(const IpAddress& ip) const
{
    const auto found = arp_cache_.find(ip);
    if (found == arp_cache_.end() || found->second.expires_at_ms <= now_ms_) {
        return std::nullopt;
    }

    return found->second.mac;
}

std::size_t NetworkInterface::waiting_count(const IpAddress& next_hop) const
{
    const auto found = waiting_.find(next_hop);
    return found == waiting_.end() ? 0 : found->second.size();
}

} // namespace tinyarp
